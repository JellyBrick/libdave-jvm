# libdave-jvm

**Discord Audio & Video End-to-End Encryption (DAVE) for Java.**

This repository contains a Java implementation of [Discord's E2EE DAVE protocol](https://daveprotocol.com/) supporting libraries.

This project only provides the cryptographic support and the complex parts of the MLS protocol - it does not communicate with any servers directly. 
It is intended to be consumed by other libraries that interact with Discord's voice servers or API, such as [Koe](https://github.com/KyokoBot/koe) or [JDA](https://github.com/discord-jda/JDA).

## Modules

- **`api`**: Defines the common Java interfaces for the DAVE protocol (Session, Encryptor, Decryptor, etc.).
- **`impl-jni`**: An implementation of the API that binds to the official C++ `libdave` using JNI. Strongly recommended for production use.
- **`natives`**: Contains the CMake project for `libdave` JNI bindings and supporting Gradle project that handles publishing the natives to a Maven repository.

## Usage

### Usage with Netty

To use the libdave-jvm with Netty's `ByteBuf`, you can add an optional dependency on Netty Buffer and use the utility classes provided in `moe.kyokobot.libdave.netty`.

**Note:** The Netty integration is *optional*. The `moe.kyokobot.libdave.netty` package is not usable unless you manually 
add the Netty dependency, because `netty-buffer` is not declared as a transitive dependency. Attempting to use these classes 
without Netty in your classpath will result in a `NoClassDefFoundError`.

**Add to your dependencies:**
```kotlin
implementation("io.netty:netty-buffer:4.2.10.Final")
```

See the `moe.kyokobot.libdave.netty` package documentation for further details.

### Dependencies

**Gradle (Kotlin DSL):**

```kotlin
repositories {
    // mavenCentral() // not published yet
}

dependencies {
    // This will transitively include the `api` module.
    implementation("moe.kyokobot.libdave:impl-jni:1.0-SNAPSHOT")

    // Linux (glibc 2.28 / EL8)
    implementation("moe.kyokobot.libdave:natives-linux-x86-64:1.0-SNAPSHOT")
    // Linux (glibc 2.35)
    implementation("moe.kyokobot.libdave:natives-linux-x86:1.0-SNAPSHOT")
    implementation("moe.kyokobot.libdave:natives-linux-aarch64:1.0-SNAPSHOT")
    implementation("moe.kyokobot.libdave:natives-linux-arm:1.0-SNAPSHOT")

    // Linux (musl)
    implementation("moe.kyokobot.libdave:natives-linux-musl-x86-64:1.0-SNAPSHOT")
    implementation("moe.kyokobot.libdave:natives-linux-musl-x86:1.0-SNAPSHOT")
    implementation("moe.kyokobot.libdave:natives-linux-musl-aarch64:1.0-SNAPSHOT")
    implementation("moe.kyokobot.libdave:natives-linux-musl-arm:1.0-SNAPSHOT")

    // Windows
    implementation("moe.kyokobot.libdave:natives-win-x86-64:1.0-SNAPSHOT")
    implementation("moe.kyokobot.libdave:natives-win-x86:1.0-SNAPSHOT")
    implementation("moe.kyokobot.libdave:natives-win-aarch64:1.0-SNAPSHOT")

    // macOS
    implementation("moe.kyokobot.libdave:natives-darwin:1.0-SNAPSHOT") // Universal Intel + Apple Silicon
}
```

## License

This project is licensed under the Apache 2.0 License. See the [LICENSE](LICENSE) file for details.
