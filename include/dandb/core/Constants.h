#pragma once

#include <cstdint>

namespace dandb {
    namespace core {

        using PageId = std::uint32_t;
        using SlotId = std::uint16_t;

        inline constexpr std::uint32_t PAGE_SIZE_BYTES = 4096;
        inline constexpr PageId INVALID_PAGE_ID = 0xFFFFFFFF;
        inline constexpr SlotId INVALID_SLOT_ID = 0xFFFF;
        inline constexpr PageId FIRST_ALLOCATABLE_PAGE_ID = 1;

    }
}
