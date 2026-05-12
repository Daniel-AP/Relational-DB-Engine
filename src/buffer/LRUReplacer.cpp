#include <dandb/buffer/LRUReplacer.h>

#include <string>
#include <iterator>

namespace dandb {
    namespace buffer {

        LRUReplacer::LRUReplacer(size_t capacity)
            : capacity_(capacity), lruList_{}, slotPositions_{}
        {}

        size_t LRUReplacer::capacity() const {
            return capacity_;
        }

        size_t LRUReplacer::size() const {
            return lruList_.size();
        }

        dandb::core::Status LRUReplacer::pin(size_t slotId) {

            if(slotId >= capacity_) {
                return dandb::core::Status::InvalidArgument("Unable to pin slot " + std::to_string(slotId) + ": slot does not exist");
            }

            const auto it = slotPositions_.find(slotId);

            if(it != slotPositions_.end()) {
                lruList_.erase(it->second);
                slotPositions_.erase(it);
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Status LRUReplacer::unpin(size_t slotId) {

            if(slotId >= capacity_) {
                return dandb::core::Status::InvalidArgument("Unable to unpin slot " + std::to_string(slotId) + ": slot does not exist");
            }

            const auto it = slotPositions_.find(slotId);

            if(it == slotPositions_.end()) {
                lruList_.push_back(slotId);
                slotPositions_.insert({ slotId, std::prev(lruList_.end()) });
            }

            return dandb::core::Status::Ok();

        }

        dandb::core::Result<size_t> LRUReplacer::getVictim() {

            if(lruList_.empty()) {
                return dandb::core::Status::NotFound("Unable to get victim slot: no evictable slots");
            }

            size_t victim = lruList_.front();

            lruList_.pop_front();
            slotPositions_.erase(victim);
            
            return victim;

        }

    }
}