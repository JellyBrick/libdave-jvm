#ifndef JNI_UTILS
#define JNI_UTILS

#include <array>
#include <cstdint>
#include <functional>
#include <jni.h>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace kyoko::libdave {

template <size_t N> class LocalRefHolder {
private:
  JNIEnv *env_;
  std::array<jobject, N> refs_;
  size_t count_;

public:
  explicit LocalRefHolder(JNIEnv *env) noexcept
      : env_(env), refs_{}, count_(0) {}

  ~LocalRefHolder() {
    for (size_t i = 0; i < count_; ++i) {
      if (refs_[i] != nullptr) {
        env_->DeleteLocalRef(refs_[i]);
      }
    }
  }

  LocalRefHolder(const LocalRefHolder &) = delete;
  LocalRefHolder &operator=(const LocalRefHolder &) = delete;
  LocalRefHolder(LocalRefHolder &&) = delete;
  LocalRefHolder &operator=(LocalRefHolder &&) = delete;

  template <typename T> T track(T ref) noexcept {
    if (count_ < N) {
      refs_[count_++] = static_cast<jobject>(ref);
    }
    return ref;
  }

  bool canTrack() const noexcept { return count_ < N; }

  size_t size() const noexcept { return count_; }
};

static inline ::jstring toJString(JNIEnv *env, const std::string &str) {
  return env->NewStringUTF(str.c_str());
}

static inline bool copyByteArrayToVector(JNIEnv *env, jbyteArray array,
                           std::vector<uint8_t> &vector) {
  jsize length = env->GetArrayLength(array);
  if (length < 0) {
    return false;
  }
  vector.resize(length);
  env->GetByteArrayRegion(array, 0, length,
                          reinterpret_cast<jbyte *>(vector.data()));
  return true;
}

static inline ::jbyteArray toByteArray(JNIEnv *env, const std::vector<uint8_t> &vector) {
  auto arraySize = static_cast<jsize>(vector.size());
  auto array = env->NewByteArray(arraySize);
  env->SetByteArrayRegion(array, 0, arraySize,
                          reinterpret_cast<const jbyte *>(vector.data()));
  return array;
}

static inline void throwIllegalArgument(JNIEnv *env, const char *message) {
  LocalRefHolder<1> holder(env);
  jclass exc =
      holder.track(env->FindClass("java/lang/IllegalArgumentException"));
  if (exc != nullptr) {
    env->ThrowNew(exc, message);
  }
}

static inline ::jobject boxedInteger(JNIEnv *env, int value) {
  struct IntegerCache {
    jclass klass;
    jmethodID valueOf;
    bool ok;
  };

  static IntegerCache cache{nullptr, nullptr, false};
  static std::once_flag initOnce;

  std::call_once(initOnce, [env]() {
    LocalRefHolder<1> refs(env);
    jclass localKlass = refs.track(env->FindClass("java/lang/Integer"));
    if (localKlass == nullptr) {
      return;
    }

    cache.klass = static_cast<jclass>(env->NewGlobalRef(localKlass));
    if (cache.klass == nullptr) {
      return;
    }

    cache.valueOf =
        env->GetStaticMethodID(cache.klass, "valueOf", "(I)Ljava/lang/Integer;");
    if (cache.valueOf == nullptr) {
      env->DeleteGlobalRef(cache.klass);
      cache.klass = nullptr;
      return;
    }

    cache.ok = true;
  });

  if (!cache.ok) {
    return nullptr;
  }

  return env->CallStaticObjectMethod(cache.klass, cache.valueOf,
                                     static_cast<jint>(value));
}

struct DirectBufferInfo {
  uint8_t *address;
  size_t length;
};

static inline bool getDirectBufferInfo(JNIEnv *env, jobject buffer, DirectBufferInfo &info) {
  void *addr = env->GetDirectBufferAddress(buffer);
  if (addr == nullptr) {
    return false;
  }

  struct BufferMethodCache {
    jclass bufferClass;
    jmethodID position;
    jmethodID limit;
    bool ok;
  };

  static BufferMethodCache cache{nullptr, nullptr, nullptr, false};
  static std::once_flag initOnce;

  std::call_once(initOnce, [env]() {
    // Get java.nio.Buffer class to access position/limit
    // We can assume buffer is an instance of it.
    // Using FindClass("java/nio/Buffer") is safer than GetObjectClass because
    // the object might be a specific subclass (DirectByteBuffer) but methods are
    // on Buffer.
    LocalRefHolder<1> holder(env);
    jclass localBufferClass = holder.track(env->FindClass("java/nio/Buffer"));
    if (localBufferClass == nullptr) {
      return;
    }

    cache.bufferClass = static_cast<jclass>(env->NewGlobalRef(localBufferClass));
    if (cache.bufferClass == nullptr) {
      return;
    }

    cache.position = env->GetMethodID(cache.bufferClass, "position", "()I");
    cache.limit = env->GetMethodID(cache.bufferClass, "limit", "()I");

    if (cache.position == nullptr || cache.limit == nullptr) {
      env->DeleteGlobalRef(cache.bufferClass);
      cache.bufferClass = nullptr;
      cache.position = nullptr;
      cache.limit = nullptr;
      return;
    }

    cache.ok = true;
  });

  if (!cache.ok) {
    return false;
  }

  jint position = env->CallIntMethod(buffer, cache.position);
  jint limit = env->CallIntMethod(buffer, cache.limit);

  if (position < 0 || limit < position) {
    return false;
  }

  info.address = static_cast<uint8_t *>(addr) + position;
  info.length = static_cast<size_t>(limit - position);

  return true;
}

class JNICallbackWrapper {
private:
  JavaVM *jvm;
  jobject callback;
  jmethodID methodId;

  void callMethod(JNIEnv *env, const std::string &arg1,
                  const std::string &arg2) {
    LocalRefHolder<2> holder(env);
    jstring jarg1 = holder.track(toJString(env, arg1));
    jstring jarg2 = holder.track(toJString(env, arg2));

    env->CallVoidMethod(callback, methodId, jarg1, jarg2);
  }

  void callMethod(JNIEnv *env, jbyteArray arg1) {
    LocalRefHolder<1> holder(env);
    holder.track(arg1);
    env->CallVoidMethod(callback, methodId, arg1);
  }

  void callMethod(JNIEnv *env) {
    env->CallVoidMethod(callback, methodId);
  }

public:
  JNICallbackWrapper(JNIEnv *env, jobject callback, const char *methodName,
                     const char *signature)
      : jvm(nullptr), callback(nullptr), methodId(nullptr) {

    if (callback == nullptr) {
      return;
    }

    // Get JavaVM for thread attachment
    if (env->GetJavaVM(&jvm) != JNI_OK) {
      return;
    }

    // Create global reference (survives across threads and JNI calls)
    this->callback = env->NewGlobalRef(callback);
    if (this->callback == nullptr) {
      return;
    }

    // Get method ID
    LocalRefHolder<1> holder(env);
    jclass callbackClass = holder.track(env->GetObjectClass(this->callback));
    methodId = env->GetMethodID(callbackClass, methodName, signature);

    if (methodId == nullptr) {
      env->DeleteGlobalRef(this->callback);
      this->callback = nullptr;
    }
  }

  ~JNICallbackWrapper() {
    if (callback != nullptr && jvm != nullptr) {
      JNIEnv *env = getEnv();
      if (env != nullptr) {
        env->DeleteGlobalRef(callback);
      }
    }
  }

  JNICallbackWrapper(const JNICallbackWrapper &) = delete;
  JNICallbackWrapper &operator=(const JNICallbackWrapper &) = delete;
  JNICallbackWrapper(JNICallbackWrapper &&other) noexcept
      : jvm(other.jvm), callback(other.callback), methodId(other.methodId) {
    other.jvm = nullptr;
    other.callback = nullptr;
    other.methodId = nullptr;
  }

  bool isValid() const { return callback != nullptr && methodId != nullptr; }

  JNIEnv *getEnv() const {
    if (jvm == nullptr) {
      return nullptr;
    }

    JNIEnv *env = nullptr;
    jint result = jvm->GetEnv((void **)&env, JNI_VERSION_1_6);

    if (result == JNI_EDETACHED) {
      result = jvm->AttachCurrentThread((void **)&env, nullptr);
      if (result != JNI_OK) {
        return nullptr;
      }
    }

    return env;
  }

  template <typename... Args> void invoke(Args... args) {
    if (!isValid()) {
      return;
    }

    JNIEnv *env = getEnv();
    if (env == nullptr) {
      return;
    }

    callMethod(env, args...);

    if (env->ExceptionCheck()) {
      env->ExceptionDescribe();
      env->ExceptionClear();
    }
  }
};

void shutUpDave();

} // namespace kyoko::libdave

#endif // JNI_UTILS
