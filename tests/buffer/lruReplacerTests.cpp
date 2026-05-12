#include <dandb/buffer/LRUReplacer.h>
#include <dandb/core/Status.h>

#include <catch2/catch_test_macros.hpp>

#include <cstddef>

TEST_CASE("LRUReplacer starts empty with a fixed capacity", "[buffer][lru]") {

    const dandb::buffer::LRUReplacer replacer(3);

    REQUIRE(replacer.capacity() == 3);
    REQUIRE(replacer.size() == 0);

}

TEST_CASE("Empty LRUReplacer has no victim", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    const auto victim = replacer.getVictim();

    REQUIRE_FALSE(victim.ok());
    REQUIRE(victim.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE(replacer.size() == 0);

}

TEST_CASE("Unpin makes a slot evictable", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    const auto unpinStatus = replacer.unpin(1);
    const auto victim = replacer.getVictim();

    REQUIRE(unpinStatus.ok());
    REQUIRE(victim.ok());
    REQUIRE(victim.value() == 1);
    REQUIRE(replacer.size() == 0);

}

TEST_CASE("Pin removes an evictable slot from LRUReplacer", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    REQUIRE(replacer.unpin(1).ok());
    REQUIRE(replacer.size() == 1);

    const auto pinStatus = replacer.pin(1);
    const auto victim = replacer.getVictim();

    REQUIRE(pinStatus.ok());
    REQUIRE(replacer.size() == 0);
    REQUIRE_FALSE(victim.ok());
    REQUIRE(victim.status().code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("Pinning a slot that is not evictable is a no-op", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    const auto pinStatus = replacer.pin(2);

    REQUIRE(pinStatus.ok());
    REQUIRE(replacer.size() == 0);

}

TEST_CASE("Repeated unpin does not duplicate a slot", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    REQUIRE(replacer.unpin(1).ok());
    REQUIRE(replacer.unpin(1).ok());
    REQUIRE(replacer.unpin(1).ok());

    REQUIRE(replacer.size() == 1);

    const auto firstVictim = replacer.getVictim();
    const auto secondVictim = replacer.getVictim();

    REQUIRE(firstVictim.ok());
    REQUIRE(firstVictim.value() == 1);
    REQUIRE_FALSE(secondVictim.ok());
    REQUIRE(secondVictim.status().code() == dandb::core::StatusCode::NotFound);
    REQUIRE(replacer.size() == 0);

}

TEST_CASE("Victim is the least recently unpinned slot", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(4);

    REQUIRE(replacer.unpin(2).ok());
    REQUIRE(replacer.unpin(0).ok());
    REQUIRE(replacer.unpin(3).ok());

    const auto firstVictim = replacer.getVictim();
    const auto secondVictim = replacer.getVictim();
    const auto thirdVictim = replacer.getVictim();
    const auto emptyVictim = replacer.getVictim();

    REQUIRE(firstVictim.ok());
    REQUIRE(firstVictim.value() == 2);
    REQUIRE(secondVictim.ok());
    REQUIRE(secondVictim.value() == 0);
    REQUIRE(thirdVictim.ok());
    REQUIRE(thirdVictim.value() == 3);
    REQUIRE_FALSE(emptyVictim.ok());
    REQUIRE(emptyVictim.status().code() == dandb::core::StatusCode::NotFound);

}

TEST_CASE("Pinning one slot preserves the LRU order of the remaining slots", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(4);

    REQUIRE(replacer.unpin(0).ok());
    REQUIRE(replacer.unpin(1).ok());
    REQUIRE(replacer.unpin(2).ok());

    REQUIRE(replacer.pin(1).ok());

    const auto firstVictim = replacer.getVictim();
    const auto secondVictim = replacer.getVictim();
    const auto emptyVictim = replacer.getVictim();

    REQUIRE(firstVictim.ok());
    REQUIRE(firstVictim.value() == 0);
    REQUIRE(secondVictim.ok());
    REQUIRE(secondVictim.value() == 2);
    REQUIRE_FALSE(emptyVictim.ok());

}

TEST_CASE("A slot can become evictable again after being pinned", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    REQUIRE(replacer.unpin(0).ok());
    REQUIRE(replacer.unpin(1).ok());
    REQUIRE(replacer.pin(0).ok());
    REQUIRE(replacer.unpin(0).ok());

    const auto firstVictim = replacer.getVictim();
    const auto secondVictim = replacer.getVictim();

    REQUIRE(firstVictim.ok());
    REQUIRE(firstVictim.value() == 1);
    REQUIRE(secondVictim.ok());
    REQUIRE(secondVictim.value() == 0);

}

TEST_CASE("LRUReplacer rejects unpin for a slot outside its capacity", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    const auto status = replacer.unpin(3);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE(replacer.size() == 0);

}

TEST_CASE("LRUReplacer rejects pin for a slot outside its capacity", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(3);

    const auto status = replacer.pin(3);

    REQUIRE_FALSE(status.ok());
    REQUIRE(status.code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE(replacer.size() == 0);

}

TEST_CASE("Zero-capacity LRUReplacer rejects every slot", "[buffer][lru]") {

    dandb::buffer::LRUReplacer replacer(0);

    const auto unpinStatus = replacer.unpin(0);
    const auto pinStatus = replacer.pin(0);
    const auto victim = replacer.getVictim();

    REQUIRE(replacer.capacity() == 0);
    REQUIRE(replacer.size() == 0);
    REQUIRE_FALSE(unpinStatus.ok());
    REQUIRE(unpinStatus.code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE_FALSE(pinStatus.ok());
    REQUIRE(pinStatus.code() == dandb::core::StatusCode::InvalidArgument);
    REQUIRE_FALSE(victim.ok());
    REQUIRE(victim.status().code() == dandb::core::StatusCode::NotFound);

}
