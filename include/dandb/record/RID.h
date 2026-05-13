#pragma once

#include <dandb/core/Constants.h>

namespace dandb {
    namespace record {

        struct RID {
            dandb::core::PageId pageId = dandb::core::INVALID_PAGE_ID;
            dandb::core::SlotId slotId = dandb::core::INVALID_SLOT_ID;

            bool operator==(const RID&) const = default;
        };

    }
}