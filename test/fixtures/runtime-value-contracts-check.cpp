#include <bblite/js_data.hpp>
#include <bblite/runtime.hpp>
#include <bblite/ts_runtime.hpp>
#include <cassert>
#include <iostream>
#include <type_traits>

using namespace bbl;

static_assert(std::is_same_v<ts::ArrayBuffer, js::ArrayBuffer>);
static_assert(std::is_same_v<ts::Uint8Array, js::U8Array>);
static_assert(std::is_same_v<ts::DataView, js::DataView>);

struct Point {
    double x, y, z;
};

/** A payload that describes its edges joins the collector's registry. */
struct TracedPoint : Point {
    void gc_trace(const js::TraceVisitor&) const {}
};

struct CountedValue {
    inline static int copies = 0;
    std::string value;

    explicit CountedValue(std::string text) : value(std::move(text)) {}
    CountedValue(const CountedValue& other) : value(other.value) { ++copies; }
    CountedValue(CountedValue&&) = default;
    CountedValue& operator=(const CountedValue& other) {
        value = other.value;
        ++copies;
        return *this;
    }
    CountedValue& operator=(CountedValue&&) = default;
    bool operator==(const CountedValue&) const = default;
};

int main() {
    {
        CountedValue temporary{"transferred"};
        const auto transferred = js::take_temporary(temporary);
        assert(transferred.value == "transferred" && CountedValue::copies == 0);
        double scalar = 7;
        assert(js::take_temporary(scalar) == 7 && scalar == 7);
        auto reference = js::make_ref<TracedPoint>(TracedPoint{{1, 2, 3}});
        const auto* node = js::gc::registry.nodes.back();
        assert(node->owners() == 1);
        auto owner = js::take_temporary(reference);
        assert(!reference && owner->x == 1 && node->owners() == 1);
    }
    const Sprite2DLayerRecord layer;
    assert(layer.dirty_sprite_begin == invalid_handle && layer.dirty_sprite_end == 0);
    assert(layer.pipeline_version == 0);
    const BillboardSystemRecord billboard;
    assert(billboard.instance_version == 0);
    const std::vector<double> values{-1.9, 65537.9, 3.5};
    assert(js::array_length(values) == 3.0);
    const auto bytes = js::u8_array_from(values);
    const auto shorts = js::u16_array_from(values);
    const auto words = js::u32_array_from(values);
    const auto floats = js::f32_array_from(values);
    assert(bytes[0] == 255 && bytes[1] == 1 && bytes[2] == 3);
    assert(shorts[0] == 65535 && shorts[1] == 1);
    assert(words[0] == 4294967295u && words[1] == 65537u);
    assert(floats[2] == 3.5f);
    assert(js::array_length(js::u8_array_from(std::vector<double>{})) == 0.0);

    const std::vector<Point> points{{1.25, 2.5, 3.75}, {-4, -5, -6}};
    std::vector<js::Ref<Point>> references;
    for (const auto& point : points)
        references.push_back(js::make_ref<Point>(point));
    const auto direct = vec3_path(points);
    const auto indirect = vec3_path(references);
    assert(direct.size() == 2 && indirect.size() == 2);
    for (std::size_t i = 0; i < direct.size(); ++i) {
        assert(direct[i].x == indirect[i].x && direct[i].y == indirect[i].y &&
               direct[i].z == indirect[i].z);
    }
    references[0]->x = 9;
    assert(vec3_path(references)[0].x == 9 && vec3_path(points)[0].x == 1.25);

    {
        Scene scene;
        Scene copied = scene;
        Scene assigned;
        assert(!assigned.shares_identity(scene));
        assigned = scene;
        copied.fixed_delta_ms = 12.5;
        assert(assigned.shares_identity(scene) && assigned.fixed_delta_ms == 12.5);
        int called = 0;
        copied.disposables.push_back([&] { ++called; });
        assigned.disposables.front()();
        assert(called == 1 && scene.disposables.size() == 1);
        assigned.disposed = true;
        assert(scene.disposed && copied.disposed);
    }
    const auto nodes = js::managed_node_count();
    {
        Scene scene;
        scene.disposables.push_back(js::make_closure(
            std::tuple{scene}, [](auto& captures) { std::get<0>(captures).disposed = true; }));
        assert(js::collect_cycles() == 0);
        scene.disposables.front()();
        assert(scene.disposed);
    }
    assert(js::collect_cycles() > 0);
    assert(js::managed_node_count() == nodes);

    {
        js::WeakMap<js::Ref<Point>> map;
        auto alias = map;
        auto key = js::make_ref<Point>(Point{1, 2, 3});
        auto same_key = key;
        const auto identity = key.weak_identity();
        auto value = js::make_ref<Point>(Point{4, 5, 6});
        const auto value_identity = value.weak_identity();
        map.set(identity, value);
        value = {};
        key = {};
        assert(!identity.expired() && alias.get(same_key.weak_identity())->x == 4);
        assert(!map.get(js::make_ref<Point>(Point{1, 2, 3}).weak_identity()));
        same_key = {};
        assert(identity.expired());
        assert(!alias.get(identity));
        assert(value_identity.expired());
    }
    assert(js::managed_node_count() == nodes);

    {
        js::WeakMap<js::Ref<Point>> map;
        std::vector<js::Ref<Point>> keys;
        std::vector<js::WeakIdentity> weak_values;
        for (int i = 0; i < 65; ++i) {
            auto key = js::make_ref<Point>();
            auto value = js::make_ref<Point>();
            weak_values.push_back(value.weak_identity());
            map.set(key.weak_identity(), value);
            keys.push_back(key);
        }
        auto live = keys.back();
        keys.clear();
        for (int i = 0; i < 9; ++i)
            assert(map.get(live.weak_identity()));
        for (std::size_t i = 0; i + 1 < weak_values.size(); ++i)
            assert(weak_values[i].expired());
        assert(!weak_values.back().expired());
        live = {};
        js::collect_cycles();
        assert(weak_values.back().expired());
    }
    assert(js::managed_node_count() == nodes);

    {
        js::Map<std::string, CountedValue> map;
        map.set("first", CountedValue{"original"});
        assert(CountedValue::copies == 0 && map.at("first").value == "original");
        map.set("first", CountedValue{"replacement"});
        assert(CountedValue::copies == 0 && map.at("first").value == "replacement");
        auto alias = map;
        const CountedValue retained{"retained"};
        alias.set("second", retained);
        assert(CountedValue::copies == 1 && retained.value == "retained");
        map.set("first", retained);
        assert(CountedValue::copies == 2 && map.at("first").value == "retained");
        map.set("first", std::move(map.at("first")));
        assert(CountedValue::copies == 2 && alias.at("first").value == "retained");
        map.set("third", map.at("first"));
        assert(CountedValue::copies == 3 && map.at("third").value == "retained");

        auto entry = map.begin();
        assert(entry->first == "first");
        assert(map.erase(entry->first));
        assert(!alias.has("first"));
        alias.set("first", CountedValue{"reinserted"});
        assert(map.has("first") && map.at("first").value == "reinserted");
        ++entry;
        assert(entry->first == "second");
        ++entry;
        assert(entry->first == "third");
        ++entry;
        assert(entry->first == "first" && entry->second.value == "reinserted");
        ++entry;
        assert(entry == map.end() && map.size() == 3);

        js::Map<std::string, std::string> strings;
        std::string shared = "shared";
        strings.set(shared, std::move(shared));
        assert(strings.at("shared") == "shared");
        strings.set("shared", std::move(strings.at("shared")));
        assert(strings.at("shared") == "shared");
        strings.set("literal", "converted");
        assert(strings.at("literal") == "converted");

        // The index reads keys from the entries, so a new key is copied once,
        // into its entry, and before the aliased value moves.
        js::Map<CountedValue, CountedValue> keyed;
        CountedValue key{"aliased"};
        CountedValue::copies = 0;
        keyed.set(key, std::move(key));
        assert(CountedValue::copies == 1);
        assert(keyed.at(CountedValue{"aliased"}).value == "aliased");
    }
    assert(js::managed_node_count() == nodes);

    SharedTextureBytes original{std::vector<std::uint8_t>{1, 2, 3}};
    auto copied = original;
    assert(std::as_const(original).data() == std::as_const(copied).data());
    copied[0] = 9;
    assert(std::as_const(original)[0] == 1 && std::as_const(copied)[0] == 9);
    assert(std::as_const(original).data() != std::as_const(copied).data());
    TextureData texture;
    texture.bytes = original;
    assert(std::as_const(texture.bytes).data() == std::as_const(original).data());

    js::Tuple<3> tuple{1, 2, 3};
    auto alias = tuple;
    auto clone = js::clone_tuple(tuple);
    alias[1] = 7;
    assert(tuple[1] == 7 && clone[1] == 2);

    js::Array<double> range{1, 2, 3, 4, 5};
    js::array_fill_range(range, 9.0, -3.8, -1.2);
    assert(range[1] == 2 && range[2] == 9 && range[3] == 9 && range[4] == 5);
    js::array_copy_within(range, 1.9, 0.0, 4.0);
    assert(range[0] == 1 && range[1] == 1 && range[2] == 2 && range[3] == 9 && range[4] == 9);
    const auto tail = js::array_slice(range, -2.9, std::numeric_limits<double>::infinity());
    assert(tail.size() == 2 && tail[0] == 9 && tail[1] == 9);
    js::array_fill_range(range, 8.0, std::numeric_limits<double>::quiet_NaN(), -4.0);
    assert(range[0] == 8 && range[1] == 1);

    // Index acceptance: integers inside the container, -0 as 0, nothing else.
    {
        const std::vector<double> five(5, 0.0);
        const double infinity = std::numeric_limits<double>::infinity();
        assert(js::array_has_index(five, 0.0) && js::array_has_index(five, -0.0));
        assert(js::array_has_index(five, 4.0) && js::accepted_index(4.0) == 4);
        for (const double rejected : {5.0, -1.0, 0.5, 4.000000001, infinity, -infinity,
                                      std::numeric_limits<double>::quiet_NaN(), 9.3e18, 1e300})
            assert(!js::array_has_index(five, rejected));
    }

    // A view copy is the same object; a new view over the same bytes is not.
    {
        js::U8Array whole(4);
        whole[1] = 7;
        const js::U8Array same = whole;
        const js::U8Array view(whole.buffer(), 1, 2);
        assert(same == whole && !(view == whole) && view[0] == 7 && view.byte_offset() == 1);
        assert(view.subarray(0, 1).data() == view.data() && !(view.subarray(0, 1) == view));
        const js::U8Array copy = view.slice(0, 2);
        assert(copy.data() != view.data() && copy[0] == 7 && copy.byte_length() == 2);
        assert(js::ArrayBufferView(view).identity() == view.identity());
        const js::U8Array empty;
        assert(empty.size() == 0 && !(empty == js::U8Array{}));
        // A moved-from view is empty and bufferless, never an alias.
        js::U8Array source(3);
        const js::U8Array moved = std::move(source);
        assert(moved.size() == 3 && source.size() == 0 && source.data() == nullptr);
        assert(source.buffer().byte_length() == 0 && source.byte_offset() == 0 &&
               source.subarray(0, 1).size() == 0);
    }

    // Lookups answer as the index does across every mutation.
    {
        js::Map<std::string, double> chunks;
        const std::vector<std::string> keys{"0,0", "1,0",  "-1,0",
                                            "0,1", "0,-1", "a-longer-key,17,23"};
        for (std::size_t round = 0; round < 3; ++round) {
            for (std::size_t index = 0; index < keys.size(); ++index) {
                const std::string& key = keys[index];
                assert(!chunks.has(key));
                chunks.set(key, static_cast<double>(index));
                assert(chunks.has(key) && *chunks.get(key) == static_cast<double>(index));
                assert(*chunks.get(keys[0]) == 0.0 &&
                       *chunks.get(key) == static_cast<double>(index));
            }
            for (const std::string& key : keys)
                assert(chunks.erase(key) && !chunks.has(key) && !chunks.get(key));
        }
        js::Map<double, js::Ref<Point>> table;
        table.set(3.0, js::make_ref<Point>(Point{3, 0, 0}));
        assert(table.get(3.0)->x == 3 && !table.get(3.5) && !table.get(2.0) && !table.get(-0.5));
        table.set(-0.0, js::make_ref<Point>(Point{0, 0, 0}));
        assert(table.get(0.0) && table.get(-0.0) && !table.get(1.0));
        assert(table.erase(3.0) && !table.get(3.0) && !table.get(3.0));
        assert(&table.get(7.0) == &table.get(8.0)); // One shared empty answer.
    }

    // SameValueZero keys: every NaN is one key, -0 is the +0 key and is
    // stored as +0, as Map.prototype.set and Set.prototype.add store it.
    {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        const double other_nan = std::bit_cast<double>(std::uint64_t{0xFFF8000000000001ull});
        assert(std::isnan(other_nan));
        js::Map<double, double> numbers;
        numbers.set(nan, 1.0);
        numbers.set(other_nan, 2.0);
        assert(numbers.size() == 1 && *numbers.get(nan) == 2.0 && numbers.has(-nan));
        numbers.set(-0.0, 3.0);
        assert(numbers.size() == 2 && *numbers.get(0.0) == 3.0 && *numbers.get(-0.0) == 3.0);
        numbers.set(0.0, 4.0);
        assert(numbers.size() == 2 && *numbers.get(-0.0) == 4.0);
        for (const auto& [number, value] : numbers)
            assert(std::isnan(number) || (number == 0.0 && !std::signbit(number) && value == 4.0));
        assert(numbers.erase(other_nan) && !numbers.has(nan) && numbers.size() == 1);
        assert(numbers.erase(0.0) && numbers.size() == 0);

        js::Set<double> set;
        set.add(-0.0);
        set.add(0.0);
        set.add(nan);
        set.add(other_nan);
        assert(set.size() == 2 && set.has(0.0) && set.has(-0.0) && set.has(nan));
        assert(!std::signbit(*set.begin()));

        js::Map<std::variant<double, std::string>, double> mixed;
        mixed.set(nan, 1.0);
        mixed.set(other_nan, 2.0);
        mixed.set(std::string("NaN"), 3.0);
        mixed.set(-0.0, 4.0);
        assert(mixed.size() == 3 && *mixed.get(nan) == 2.0 && *mixed.get(0.0) == 4.0);
        assert(*mixed.get(std::string("NaN")) == 3.0 && !mixed.get(std::string("0")));
    }

    // A hot number-keyed map indexes its small integer keys by value, and
    // every insert and erase updates only its own key's position: an insert
    // after a miss is found, an erase is missed, and the keys around them
    // keep answering, through growth, clear and iteration.
    {
        js::Map<double, double> hot;
        for (int key = 0; key < 8; ++key)
            hot.set(static_cast<double>(key), key * 10.0);
        hot.set(300.0, 3000.0);
        hot.set(-1.0, -10.0);
        hot.set(2.5, 25.0);
        for (int read = 0; read < 200; ++read)
            assert(*hot.get(static_cast<double>(read % 8)) == (read % 8) * 10.0);
        assert(!hot.get(12.0) && !hot.has(200.0) && !hot.get(255.0));
        hot.set(12.0, 120.0);
        assert(*hot.get(12.0) == 120.0 && *hot.get(7.0) == 70.0 && *hot.get(0.0) == 0.0);
        hot.set(200.0, 2000.0);
        assert(*hot.get(200.0) == 2000.0 && *hot.get(12.0) == 120.0 && *hot.get(3.0) == 30.0);
        assert(hot.erase(3.0) && !hot.get(3.0) && !hot.has(3.0) && *hot.get(4.0) == 40.0);
        hot.set(3.0, 33.0);
        assert(*hot.get(3.0) == 33.0 && *hot.get(-0.0) == 0.0);
        assert(*hot.get(300.0) == 3000.0 && *hot.get(-1.0) == -10.0 && *hot.get(2.5) == 25.0);
        assert(!hot.get(256.0) && !hot.get(1.5) && !hot.get(-2.0) && hot.size() == 13);
        auto entry = hot.begin();
        assert(hot.erase(entry->first) && !hot.has(0.0));
        hot.set(0.0, 1.0);
        assert(*hot.get(0.0) == 1.0);
        hot.clear();
        assert(hot.size() == 0 && !hot.get(1.0) && !hot.get(12.0) && !hot.get(300.0));
        ++entry;
        assert(entry == hot.end());
        hot.set(1.0, 11.0);
        assert(*hot.get(1.0) == 11.0 && !hot.get(2.0) && hot.size() == 1);
    }

    // Every byte of a key reaches its hash, whatever shape its tail takes,
    // and the seed and the length reach it too.
    {
        for (std::size_t size = 0; size <= 24; ++size) {
            std::string text(size, 'k');
            const std::uint64_t base = hash_bytes(text.data(), size);
            assert(base != hash_bytes(text.data(), size, 1));
            assert(size == 0 || base != hash_bytes(text.data(), size - 1));
            for (std::size_t index = 0; index < size; ++index) {
                text[index] ^= 1;
                assert(hash_bytes(text.data(), size) != base);
                text[index] ^= 1;
            }
        }
    }

    // A cleared map refilled every frame keeps its index cells, and a bulk
    // load's index past the kept size is released; either way the refilled
    // map answers as a new one would.
    {
        js::Map<std::string, double> frame;
        for (int round = 0; round < 3; ++round) {
            for (int key = 0; key < 1000; ++key)
                frame.set(std::to_string(key + round), key);
            assert(frame.size() == 1000 && *frame.get(std::to_string(round)) == 0.0);
            assert(!frame.has(std::to_string(1000 + round)) && !frame.has(std::to_string(-1)));
            frame.clear();
            assert(frame.size() == 0 && !frame.has(std::to_string(round)));
        }
        js::Map<double, double> bulk;
        for (int key = 0; key < 60000; ++key)
            bulk.set(key, key);
        bulk.clear();
        assert(bulk.size() == 0 && !bulk.has(5.0));
        bulk.set(5.0, 1.0);
        assert(*bulk.get(5.0) == 1.0 && bulk.size() == 1 && !bulk.has(6.0));
    }

    // Erasing and re-inserting during iteration: an erased entry that has
    // not been reached is skipped, a re-inserted key moves to the end and is
    // visited there, and entries appended before the end are visited.
    {
        js::Map<std::string, double> map;
        for (const char* key : {"a", "b", "c", "d"})
            map.set(key, 0.0);
        std::string visited;
        bool moved = false;
        for (auto entry = map.begin(); entry != map.end(); ++entry) {
            const std::string key = entry->first;
            visited += key;
            if (key == "a" && !moved) {
                assert(map.erase("a") && map.erase("c"));
                map.set("a", 1.0);
                moved = true;
            } else if (key == "b") {
                map.set("e", 0.0);
            }
        }
        assert(visited == "abdae" && map.size() == 4);
        std::string order;
        for (const auto& [key, value] : map)
            order += key;
        assert(order == "bdae" && *map.get("a") == 1.0 && !map.has("c"));

        js::Set<double> numbers{1.0, 2.0, 3.0};
        std::vector<double> seen;
        for (auto value = numbers.begin(); value != numbers.end(); ++value) {
            seen.push_back(*value);
            if (*value == 1.0 && seen.size() == 1) {
                assert(numbers.erase(1.0) && numbers.erase(2.0));
                numbers.add(1.0);
            }
        }
        assert((seen == std::vector<double>{1.0, 3.0, 1.0}) && numbers.size() == 2);
    }

    // The hash index against a reference model over random operations:
    // presence, values, size and insertion order agree after each step.
    {
        std::uint64_t state = 0x2545F4914F6CDD1Dull;
        const auto next = [&state](std::uint64_t bound) {
            state ^= state << 13;
            state ^= state >> 7;
            state ^= state << 17;
            return state % bound;
        };
        js::Map<std::string, double> strings;
        js::Map<double, double> numbers;
        std::vector<std::pair<std::string, double>> string_model;
        std::vector<std::pair<double, double>> number_model;
        const auto find_in = [](auto& model, const auto& key) {
            return std::find_if(model.begin(), model.end(),
                                [&](const auto& entry) { return entry.first == key; });
        };
        for (int step = 0; step < 20000; ++step) {
            const auto operation = next(10);
            const std::string text = "k" + std::to_string(next(400));
            const double number = static_cast<double>(next(600)) - 100.0;
            const double value = static_cast<double>(step);
            if (operation < 5) {
                strings.set(text, value);
                numbers.set(number, value);
                const auto text_entry = find_in(string_model, text);
                if (text_entry == string_model.end())
                    string_model.emplace_back(text, value);
                else
                    text_entry->second = value;
                const auto number_entry = find_in(number_model, number);
                if (number_entry == number_model.end())
                    number_model.emplace_back(number, value);
                else
                    number_entry->second = value;
            } else if (operation < 8) {
                const auto text_entry = find_in(string_model, text);
                assert(strings.erase(text) == (text_entry != string_model.end()));
                if (text_entry != string_model.end())
                    string_model.erase(text_entry);
                const auto number_entry = find_in(number_model, number);
                assert(numbers.erase(number) == (number_entry != number_model.end()));
                if (number_entry != number_model.end())
                    number_model.erase(number_entry);
            } else {
                const auto text_entry = find_in(string_model, text);
                const auto text_value = strings.get(text);
                assert(text_value.has_value() == (text_entry != string_model.end()));
                assert(!text_value || *text_value == text_entry->second);
                const auto number_entry = find_in(number_model, number);
                const auto number_value = numbers.get(number);
                assert(number_value.has_value() == (number_entry != number_model.end()));
                assert(!number_value || *number_value == number_entry->second);
            }
            assert(strings.size() == string_model.size() && numbers.size() == number_model.size());
            if (step % 1000 == 999 || step == 19999) {
                std::size_t index = 0;
                for (const auto& [key, stored] : strings) {
                    assert(key == string_model[index].first &&
                           stored == string_model[index].second);
                    ++index;
                }
                index = 0;
                for (const auto& [key, stored] : numbers) {
                    assert(key == number_model[index].first &&
                           stored == number_model[index].second);
                    ++index;
                }
            }
        }
    }

    // Recent pure-function results are keyed by the arguments' exact bits.
    {
        js::RecentStrings<2> recent;
        int computed = 0;
        const auto key = [&](double left, double right) {
            return recent.remember({left, right}, [&] {
                ++computed;
                return js::number_to_string(left) + "," + js::number_to_string(right);
            });
        };
        assert(key(1, 2) == "1,2" && key(1, 2) == "1,2" && computed == 1);
        assert(key(0.0, 2) == "0,2" && key(-0.0, 2) == "0,2" && computed == 3);
        for (int step = 0; step < 6; ++step)
            assert(key(step, step) ==
                   js::number_to_string(step) + "," + js::number_to_string(step));
        assert(key(1, 2) == "1,2" && computed == 10);
    }
    std::cout << "runtime-value-contracts: ok\n";
}
