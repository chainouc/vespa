// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#include <vespa/check_require.h>
#include <vespa/searchcommon/attribute/config.h>
#include <vespa/searchlib/attribute/attributefactory.h>
#include <vespa/searchlib/attribute/attributevector.h>
#include <vespa/searchlib/attribute/floatbase.h>
#include <vespa/searchlib/attribute/integerbase.h>
#include <vespa/searchlib/fef/termfieldmatchdata.h>
#include <vespa/searchlib/query/query_term_simple.h>
#include <vespa/searchlib/queryeval/executeinfo.h>
#include <vespa/searchlib/queryeval/fake_search.h>
#include <vespa/searchlib/queryeval/same_element_search.h>
#include <vespa/vespalib/gtest/gtest.h>

#include <algorithm>
#include <limits>

#include <vespa/log/log.h>
LOG_SETUP("search_iterator_element_ids_test");

using namespace search::attribute;
using namespace search;

namespace {

AttributeVector::SP createAndFillAttribute() {
    AttributeFactory    factory;
    AttributeVector::SP attribute = factory.createAttribute("mva", Config(BasicType::INT32, CollectionType::ARRAY));
    attribute->addDocs(6);
    IntegerAttribute& ia = dynamic_cast<IntegerAttribute&>(*attribute);
    ia.append(1, 3, 1);
    for (int v : {1, 2, 3, 1, 2, 3}) {
        ia.append(2, v, 1);
    }
    for (int v : {1, 2, 3, 4, 5, 1, 2, 3, 4, 5, 6}) {
        ia.append(4, v, 1);
    }
    ia.append(5, 5, 1);
    attribute->commit();
    return attribute;
}

queryeval::FakeResult createResult() {
    queryeval::FakeResult result;
    result.doc(2).elem(0).pos(7).pos(9).elem(3).pos(1);
    result.doc(4).elem(0).pos(2).elem(5).pos(1).pos(2).pos(3);
    return result;
}

void verifySeek(queryeval::SearchIterator& itr) {
    itr.initFullRange();
    EXPECT_FALSE(itr.seek(1));
    EXPECT_TRUE(itr.seek(2));
    EXPECT_FALSE(itr.seek(3));
    EXPECT_TRUE(itr.seek(4));
    EXPECT_FALSE(itr.seek(5));
}

void verifyGetElementIds(queryeval::SearchIterator& itr, const std::vector<std::vector<uint32_t>>& expectedALL) {
    itr.initFullRange();
    std::vector<uint32_t> elems;
    for (uint32_t docId : {1, 2, 3, 4, 5}) {
        const auto& expected = expectedALL[docId];
        elems.clear();
        EXPECT_EQ(expected.empty(), !itr.seek(docId));
        CHECK(expected.empty() != itr.seek(docId));
        if (itr.seek(docId)) {
            itr.get_element_ids(docId, elems);
            EXPECT_EQ(expected.size(), elems.size());
            EXPECT_EQ(expected, elems);
        }
    }
}

void verifyMergeElementIds(queryeval::SearchIterator& itr, std::vector<uint32_t> initial,
                           const std::vector<std::vector<uint32_t>>& expectedALL) {
    itr.initFullRange();
    std::vector<uint32_t> elems;
    for (uint32_t docId : {1, 2, 3, 4, 5}) {
        const auto& expected = expectedALL[docId];
        elems = initial;
        if (itr.seek(docId)) {
            itr.and_element_ids_into(docId, elems);
            EXPECT_EQ(expected.size(), elems.size());
            EXPECT_EQ(expected, elems);
        }
    }
}

void verifyElementIterator(queryeval::SearchIterator& itr) {
    verifySeek(itr);
    std::vector<std::vector<uint32_t>> expectedALL = {{}, {}, {0, 3}, {}, {0, 5}, {}};
    std::vector<std::vector<uint32_t>> expectedNONE = {{}, {}, {}, {}, {}, {}};
    std::vector<std::vector<uint32_t>> expectedSOME = {{}, {}, {3}, {}, {5}, {}};
    verifyGetElementIds(itr, expectedALL);
    verifyMergeElementIds(itr, {0, 1, 2, 3, 4, 5}, expectedALL);
    verifyMergeElementIds(itr, {}, expectedNONE);
    verifyMergeElementIds(itr, {1, 3, 4, 5}, expectedSOME);
}

} // namespace

TEST(SearchIteratorElementIdsTest, require_that_searchcontext) {
    AttributeVector::SP     attribute = createAndFillAttribute();
    fef::TermFieldMatchData tfmd;

    SearchContextParams params;
    ISearchContext::UP  sc =
        attribute->createSearchContext(std::make_unique<QueryTermSimple>("1", QueryTermSimple::Type::WORD), params);
    auto itr = sc->createIterator(&tfmd, false);
    verifyElementIterator(*itr);
}

TEST(SearchIteratorElementIdsTest, require_that_non_searchcontext) {
    fef::TermFieldMatchData      tfmd;
    fef::TermFieldMatchDataArray tfmda;
    tfmda.add(&tfmd);
    queryeval::FakeResult result = createResult();
    auto                  search = std::make_unique<queryeval::FakeSearch>("", "", "", result, std::move(tfmda));
    verifyElementIterator(*search);
}

namespace {

void verify_numeric_elements(AttributeVector& attribute, const std::vector<const char*>& terms) {
    std::vector<std::vector<uint32_t>> candidate_sets = {{},
                                                         {0},
                                                         {0, 0},
                                                         {1, 1},
                                                         {0, 1, 2},
                                                         {0, 0, 1, 3},
                                                         {1, 3},
                                                         {0, 2, 100},
                                                         {1, 100, INT32_MAX},
                                                         {511},
                                                         {510, 511},
                                                         {511, 512},
                                                         {127, 255, 510, 511, 512}};
    candidate_sets.emplace_back();
    for (uint32_t elem = 0; elem < 512; ++elem) {
        candidate_sets.back().push_back(elem);
    }
    for (const auto* text : terms) {
        SCOPED_TRACE(text);
        auto context = attribute.createSearchContext(
            std::make_unique<QueryTermSimple>(text, QueryTermSimple::Type::WORD), SearchContextParams());
        for (uint32_t doc = 0; doc < attribute.getCommittedDocIdLimit(); ++doc) {
            SCOPED_TRACE(doc);
            std::vector<uint32_t> expected;
            std::vector<uint32_t> actual;
            // Keep the virtual-find implementation as an independent reference.
            context->ISearchContext::get_element_ids(doc, expected);
            context->get_element_ids(doc, actual);
            EXPECT_EQ(expected, actual);
            EXPECT_TRUE(std::is_sorted(actual.begin(), actual.end()));
            EXPECT_EQ(std::adjacent_find(actual.begin(), actual.end()), actual.end());
            auto appended = std::vector<uint32_t>{42};
            context->get_element_ids(doc, appended);
            expected.insert(expected.begin(), 42);
            EXPECT_EQ(expected, appended);
            for (const auto& candidates : candidate_sets) {
                auto legacy = candidates;
                auto bulk = candidates;
                context->ISearchContext::and_element_ids_into(doc, legacy);
                context->and_element_ids_into(doc, bulk);
                EXPECT_EQ(legacy, bulk);
            }
        }
        // An empty intersection must not access the document's value mapping.
        std::vector<uint32_t> empty;
        context->and_element_ids_into(UINT32_MAX, empty);
        EXPECT_TRUE(empty.empty());
    }
}

} // namespace

TEST(SearchIteratorElementIdsTest, numeric_elements_match_find_for_integer_arrays_and_weighted_sets) {
    for (auto type : {BasicType::INT8, BasicType::INT16, BasicType::INT32, BasicType::INT64}) {
        for (bool fast_search : {false, true}) {
            for (auto collection : {CollectionType::ARRAY, CollectionType::WSET}) {
                SCOPED_TRACE(::testing::Message()
                             << "type=" << type << " fast_search=" << fast_search << " collection=" << collection);
                Config config(type, collection);
                config.setFastSearch(fast_search);
                auto attribute = AttributeFactory::createAttribute("values", config);
                attribute->addDocs(6);
                auto& numeric = dynamic_cast<IntegerAttribute&>(*attribute);
                for (int v : {1, 1, 4}) {
                    ASSERT_TRUE(numeric.append(1, v, v));
                }
                for (int v : {0, 2, -2}) {
                    ASSERT_TRUE(numeric.append(2, v, v + 3));
                }
                int64_t low = (type == BasicType::INT8)    ? std::numeric_limits<int8_t>::min()
                              : (type == BasicType::INT16) ? std::numeric_limits<int16_t>::min()
                              : (type == BasicType::INT32) ? std::numeric_limits<int32_t>::min()
                                                           : std::numeric_limits<int64_t>::min();
                int64_t high = (type == BasicType::INT8)    ? std::numeric_limits<int8_t>::max()
                               : (type == BasicType::INT16) ? std::numeric_limits<int16_t>::max()
                               : (type == BasicType::INT32) ? std::numeric_limits<int32_t>::max()
                                                            : std::numeric_limits<int64_t>::max();
                ASSERT_TRUE(numeric.append(3, low, 1));
                ASSERT_TRUE(numeric.append(3, high, 1));
                // Cover long arrays, sparse matches and out-of-range candidates.
                for (int i = 0; i < 512; ++i) {
                    ASSERT_TRUE(numeric.append(4, (i % 31) - 15, 1));
                }
                ASSERT_TRUE(numeric.append(5, 7, 1));
                attribute->commit();
                verify_numeric_elements(*attribute,
                                        {"1", "[-2;4]", "[7;7]", "[9;3]", "[100;101]", "[1;3>", "[;;100]", "[;;-100]",
                                         "[-9223372036854775808;9223372036854775807]", "9223372036854775807", "127",
                                         "32767", "2147483647", "-128", "-32768", "-2147483648"});
            }
        }
    }
}

TEST(SearchIteratorElementIdsTest, numeric_elements_match_find_for_floating_arrays_and_weighted_sets) {
    for (auto type : {BasicType::FLOAT, BasicType::DOUBLE}) {
        for (bool fast_search : {false, true}) {
            for (auto collection : {CollectionType::ARRAY, CollectionType::WSET}) {
                SCOPED_TRACE(::testing::Message()
                             << "type=" << type << " fast_search=" << fast_search << " collection=" << collection);
                Config config(type, collection);
                config.setFastSearch(fast_search);
                auto attribute = AttributeFactory::createAttribute("values", config);
                attribute->addDocs(5);
                auto& numeric = dynamic_cast<FloatingPointAttribute&>(*attribute);
                for (double v : {std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
                                 -std::numeric_limits<double>::infinity()})
                {
                    ASSERT_TRUE(numeric.append(1, v, 1));
                }
                for (double v : {-2.5, 0.0, 2.5, 2.5}) {
                    ASSERT_TRUE(numeric.append(2, v, 1));
                }
                ASSERT_TRUE(numeric.append(3, 7.0, 1));
                for (int i = 0; i < 512; ++i) {
                    ASSERT_TRUE(numeric.append(4, (i % 31) - 15.5, 1));
                }
                attribute->commit();
                verify_numeric_elements(*attribute,
                                        {"[-3;3]", "[7;7]", "-2.5", "[9;3]", "[100;101]", "[1;3>", "[;;100]",
                                         "[;;-100]", "[-1.7976931348623157e308;1.7976931348623157e308]"});
            }
        }
    }
}

TEST(SearchIteratorElementIdsTest, same_element_preserves_hits_matching_elements_and_ranking) {
    for (bool fast_search : {false, true}) {
        Config config(BasicType::INT32, CollectionType::ARRAY);
        config.setFastSearch(fast_search);
        auto a = AttributeFactory::createAttribute("a", config);
        auto b = AttributeFactory::createAttribute("b", config);
        a->addDocs(6);
        b->addDocs(6);
        const std::vector<std::vector<int>> a_values = {{}, {1}, {1, 0}, {1, 0, 1}, {}, {1, 1, 1}};
        const std::vector<std::vector<int>> b_values = {{}, {1}, {0, 1}, {1, 1, 1}, {1}, {0, 1}};
        for (uint32_t doc = 0; doc < a_values.size(); ++doc) {
            for (int value : a_values[doc]) {
                ASSERT_TRUE(dynamic_cast<IntegerAttribute&>(*a).append(doc, value, 1));
            }
            for (int value : b_values[doc]) {
                ASSERT_TRUE(dynamic_cast<IntegerAttribute&>(*b).append(doc, value, 1));
            }
        }
        a->commit();
        b->commit();
        for (bool strict : {false, true}) {
            for (bool normal_features : {false, true}) {
                for (const auto& filter : std::vector<std::vector<uint32_t>>{{}, {0}, {1}, {0, 2}, {99}}) {
                    SCOPED_TRACE(::testing::Message() << "fast_search=" << fast_search << " strict=" << strict
                                                      << " filter=" << ::testing::PrintToString(filter));
                    auto a_context = a->createSearchContext(
                        std::make_unique<QueryTermSimple>("1", QueryTermSimple::Type::WORD), SearchContextParams());
                    auto b_context = b->createSearchContext(
                        std::make_unique<QueryTermSimple>("1", QueryTermSimple::Type::WORD), SearchContextParams());
                    fef::TermFieldMatchData a_md, b_md, root_md;
                    root_md.setNeedNormalFeatures(normal_features);
                    a_context->fetchPostings(queryeval::ExecuteInfo::FULL, strict);
                    b_context->fetchPostings(queryeval::ExecuteInfo::FULL, false);
                    std::vector<std::unique_ptr<queryeval::SearchIterator>> children;
                    children.push_back(a_context->createIterator(&a_md, strict));
                    children.push_back(b_context->createIterator(&b_md, false));
                    queryeval::SameElementSearch       se(root_md, {}, std::move(children), strict, filter);
                    std::vector<std::vector<uint32_t>> expected(6);
                    for (uint32_t doc = 1; doc < expected.size(); ++doc) {
                        for (uint32_t elem = 0; elem < std::min(a_values[doc].size(), b_values[doc].size()); ++elem) {
                            if (a_values[doc][elem] == 1 && b_values[doc][elem] == 1 &&
                                (filter.empty() || std::binary_search(filter.begin(), filter.end(), elem)))
                            {
                                expected[doc].push_back(elem);
                            }
                        }
                    }
                    se.initRange(1, expected.size());
                    for (uint32_t doc = 1; doc < expected.size(); ++doc) {
                        EXPECT_EQ(!expected[doc].empty(), se.seek(doc));
                        if (!expected[doc].empty()) {
                            se.unpack(doc);
                            std::vector<uint32_t> ranked_elements;
                            for (const auto& pos : root_md) {
                                ranked_elements.push_back(pos.getElementId());
                            }
                            EXPECT_EQ(normal_features ? expected[doc] : std::vector<uint32_t>(), ranked_elements);
                        }
                    }
                    // Summary collection uses a separate iterator, without seek/unpack.
                    std::vector<std::unique_ptr<queryeval::SearchIterator>> collector_children;
                    collector_children.push_back(a_context->createIterator(&a_md, strict));
                    collector_children.push_back(b_context->createIterator(&b_md, false));
                    queryeval::SameElementSearch collector(root_md, {}, std::move(collector_children), strict,
                                                           filter);
                    collector.initRange(1, expected.size());
                    for (uint32_t doc = 1; doc < expected.size(); ++doc) {
                        std::vector<uint32_t> elements;
                        collector.find_matching_elements(doc, elements);
                        EXPECT_EQ(expected[doc], elements);
                    }
                }
            }
        }
    }
}

GTEST_MAIN_RUN_ALL_TESTS()
