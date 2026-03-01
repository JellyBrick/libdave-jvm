#include "cryptor_manager.h"

/// KOE PATCH BEGIN
#include <algorithm>
/// KOE PATCH END
#include <limits>

#include <dave/logger.h>

#include <bytes/bytes.h>

using namespace std::chrono_literals;

namespace discord {
namespace dave {

/// KOE PATCH BEGIN
constexpr auto kCleanupInterval = 250ms;
/// KOE PATCH END

KeyGeneration ComputeWrappedGeneration(KeyGeneration oldest, KeyGeneration generation)
{
    // Assume generation is greater than or equal to oldest, this may be wrong in a few cases but
    // will be caught by the max generation gap check.
    auto remainder = oldest % kGenerationWrap;
    auto factor = oldest / kGenerationWrap + (generation < remainder ? 1 : 0);
    return factor * kGenerationWrap + generation;
}

BigNonce ComputeWrappedBigNonce(KeyGeneration generation, TruncatedSyncNonce nonce)
{
    // Remove the generation bits from the nonce
    auto maskedNonce = nonce & ((1 << kRatchetGenerationShiftBits) - 1);
    // Add the wrapped generation bits back in
    return static_cast<BigNonce>(generation) << kRatchetGenerationShiftBits | maskedNonce;
}

CryptorManager::CryptorManager(const IClock& clock, std::unique_ptr<IKeyRatchet> keyRatchet)
  : clock_(clock)
  , keyRatchet_(std::move(keyRatchet))
  , ratchetCreation_(clock.Now())
  , ratchetExpiry_(TimePoint::max())
{
    /// KOE PATCH BEGIN
    missingNoncesSet_.reserve(kMaxMissingNonces);
    /// KOE PATCH END
}

bool CryptorManager::CanProcessNonce(KeyGeneration generation, TruncatedSyncNonce nonce) const
{
    if (!newestProcessedNonce_) {
        return true;
    }

    auto bigNonce = ComputeWrappedBigNonce(generation, nonce);
    /// KOE PATCH BEGIN
    return bigNonce > *newestProcessedNonce_ ||
      missingNoncesSet_.find(bigNonce) != missingNoncesSet_.end();
    /// KOE PATCH END
}

ICryptor* CryptorManager::GetCryptor(KeyGeneration generation)
{
    CleanupExpiredCryptors();

    if (generation < oldestGeneration_) {
        DISCORD_LOG(LS_INFO) << "Received frame with old generation: " << generation
                             << ", oldest generation: " << oldestGeneration_;
        return nullptr;
    }

    if (generation > newestGeneration_ + kMaxGenerationGap) {
        DISCORD_LOG(LS_INFO) << "Received frame with future generation: " << generation
                             << ", newest generation: " << newestGeneration_;
        return nullptr;
    }

    auto ratchetLifetimeSec =
      std::chrono::duration_cast<std::chrono::seconds>(clock_.Now() - ratchetCreation_).count();
    auto maxLifetimeFrames = kMaxFramesPerSecond * ratchetLifetimeSec;
    auto maxLifetimeGenerations = maxLifetimeFrames >> kRatchetGenerationShiftBits;
    if (generation > maxLifetimeGenerations) {
        DISCORD_LOG(LS_INFO) << "Received frame with generation " << generation
                             << " beyond ratchet max lifetime generations: "
                             << maxLifetimeGenerations
                             << ", ratchet lifetime: " << ratchetLifetimeSec << "s";
        return nullptr;
    }

    auto it = cryptors_.find(generation);
    if (it == cryptors_.end()) {
        // We don't have a cryptor for this generation, create one
        std::tie(it, std::ignore) = cryptors_.emplace(generation, MakeExpiringCryptor(generation));
    }

    // Return a non-owning pointer to the cryptor
    auto& [cryptor, expiry] = it->second;
    return cryptor.get();
}

void CryptorManager::ReportCryptorSuccess(KeyGeneration generation, TruncatedSyncNonce nonce)
{
    auto bigNonce = ComputeWrappedBigNonce(generation, nonce);

    // Add any missing nonces to the queue
    if (!newestProcessedNonce_) {
        newestProcessedNonce_ = bigNonce;
    }
    else if (bigNonce > *newestProcessedNonce_) {
        auto missingNonces =
          std::min(bigNonce - *newestProcessedNonce_ - 1, static_cast<uint64_t>(kMaxMissingNonces));

        /// KOE PATCH BEGIN
        while (!missingNonces_.empty() &&
               missingNonces_.size() + missingNonces > kMaxMissingNonces) {
            missingNoncesSet_.erase(missingNonces_.front());
            missingNonces_.pop_front();
        }

        for (auto i = bigNonce - missingNonces; i < bigNonce; ++i) {
            missingNonces_.push_back(i);
            missingNoncesSet_.insert(i);
        }
        /// KOE PATCH END

        // Update the newest processed nonce
        newestProcessedNonce_ = bigNonce;
    }
    else {
        /// KOE PATCH BEGIN
        if (missingNoncesSet_.erase(bigNonce) > 0) {
            auto it = std::find(missingNonces_.begin(), missingNonces_.end(), bigNonce);
            if (it != missingNonces_.end()) {
                missingNonces_.erase(it);
            }
        }
        /// KOE PATCH END
    }

    if (generation <= newestGeneration_ || cryptors_.find(generation) == cryptors_.end()) {
        return;
    }
    DISCORD_LOG(LS_INFO) << "Reporting cryptor success, generation: " << generation;
    newestGeneration_ = generation;

    // Update the expiry time for all old cryptors
    const auto expiryTime = clock_.Now() + kCryptorExpiry;
    for (auto& [gen, cryptor] : cryptors_) {
        if (gen < newestGeneration_) {
            DISCORD_LOG(LS_INFO) << "Updating expiry for cryptor, generation: " << gen;
            cryptor.expiry = std::min(cryptor.expiry, expiryTime);
            /// KOE PATCH BEGIN
            earliestCryptorExpiry_ = std::min(earliestCryptorExpiry_, cryptor.expiry);
            /// KOE PATCH END
        }
    }
}

KeyGeneration CryptorManager::ComputeWrappedGeneration(KeyGeneration generation) const
{
    return ::discord::dave::ComputeWrappedGeneration(oldestGeneration_, generation);
}

CryptorManager::ExpiringCryptor CryptorManager::MakeExpiringCryptor(KeyGeneration generation)
{
    // Get the new key from the ratchet
    auto encryptionKey = keyRatchet_->GetKey(generation);
    auto expiryTime = TimePoint::max();

    // If we got frames out of order, we might have to create a cryptor for an old generation
    // In that case, create it with a non-infinite expiry time as we have already transitioned
    // to a newer generation
    if (generation < newestGeneration_) {
        DISCORD_LOG(LS_INFO) << "Creating cryptor for old generation: " << generation;
        expiryTime = clock_.Now() + kCryptorExpiry;
    }
    else {
        DISCORD_LOG(LS_INFO) << "Creating cryptor for new generation: " << generation;
    }

    /// KOE PATCH BEGIN
    earliestCryptorExpiry_ = std::min(earliestCryptorExpiry_, expiryTime);
    /// KOE PATCH END

    return {CreateCryptor(encryptionKey), expiryTime};
}

void CryptorManager::CleanupExpiredCryptors()
{
    /// KOE PATCH BEGIN
    const auto now = clock_.Now();
    if (now < nextCleanup_ && now < earliestCryptorExpiry_) {
        return;
    }

    nextCleanup_ = now + kCleanupInterval;

    for (auto it = cryptors_.begin(); it != cryptors_.end();) {
        auto& [generation, cryptor] = *it;

        bool expired = cryptor.expiry < now;
        if (expired) {
            DISCORD_LOG(LS_INFO) << "Removing expired cryptor, generation: " << generation;
        }

        it = expired ? cryptors_.erase(it) : ++it;
    }

    earliestCryptorExpiry_ = TimePoint::max();
    for (const auto& [generation, cryptor] : cryptors_) {
        (void)generation;
        earliestCryptorExpiry_ = std::min(earliestCryptorExpiry_, cryptor.expiry);
    }
    /// KOE PATCH END

    while (oldestGeneration_ < newestGeneration_ &&
           cryptors_.find(oldestGeneration_) == cryptors_.end()) {
        DISCORD_LOG(LS_INFO) << "Deleting key for old generation: " << oldestGeneration_;
        keyRatchet_->DeleteKey(oldestGeneration_);
        ++oldestGeneration_;
    }
}

} // namespace dave
} // namespace discord
