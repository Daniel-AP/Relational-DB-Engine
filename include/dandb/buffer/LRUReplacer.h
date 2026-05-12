#pragma once

#include <dandb/core/Status.h>
#include <dandb/core/Result.h>

#include <list>
#include <unordered_map>
#include <cstddef>

namespace dandb {
    namespace buffer {

        class LRUReplacer {

            public:
                explicit LRUReplacer(size_t capacity);
                
                size_t capacity() const;
                size_t size() const;

                dandb::core::Status pin(size_t slotId);
                dandb::core::Status unpin(size_t slotId);
                dandb::core::Result<size_t> getVictim();

            private:
                size_t capacity_;
            
                std::list<size_t> lruList_;
                std::unordered_map<size_t, std::list<size_t>::iterator> slotPositions_;

        };

    }
}