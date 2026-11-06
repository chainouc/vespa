// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "attributeiterators.hpp"
#include "multi_numeric_search_context.h"
#include "multi_value_mapping.h"

#include <vespa/searchlib/queryeval/emptysearch.h>

namespace search::attribute {

template <typename T, typename M>
MultiNumericSearchContext<T, M>::MultiNumericSearchContext(std::unique_ptr<QueryTermSimple> qTerm,
                                                           const AttributeVector&           toBeSearched,
                                                           MultiValueMappingReadView<M>     mv_mapping_read_view)
    : NumericSearchContext<NumericRangeMatcher<T>>(toBeSearched, *qTerm, false),
      _mv_mapping_read_view(mv_mapping_read_view) {
}

template <typename T, typename M> MultiNumericSearchContext<T, M>::~MultiNumericSearchContext() = default;

template <typename T, typename M>
std::unique_ptr<queryeval::SearchIterator>
MultiNumericSearchContext<T, M>::createFilterIterator(fef::TermFieldMatchData* matchData, bool strict) {
    if (!this->valid()) {
        return std::make_unique<queryeval::EmptySearch>();
    }
    if (this->getIsFilter()) {
        return strict ? std::make_unique<FilterAttributeIteratorStrict<MultiNumericSearchContext<T, M>>>(*this,
                                                                                                         matchData)
                      : std::make_unique<FilterAttributeIteratorT<MultiNumericSearchContext<T, M>>>(*this, matchData);
    }
    return strict ? std::make_unique<AttributeIteratorStrict<MultiNumericSearchContext<T, M>>>(*this, matchData)
                  : std::make_unique<AttributeIteratorT<MultiNumericSearchContext<T, M>>>(*this, matchData);
}

template <typename T, typename M>
uint32_t MultiNumericSearchContext<T, M>::get_committed_docid_limit() const noexcept {
    return _mv_mapping_read_view.get_committed_docid_limit();
}

template <typename T, typename M>
void MultiNumericSearchContext<T, M>::and_element_ids_into(uint32_t docid, std::vector<uint32_t>& element_ids) const {
    // Keep find-next skipping for larger candidate sets.
    if (element_ids.size() > 2) {
        ISearchContext::and_element_ids_into(docid, element_ids);
        return;
    }
    if (element_ids.empty()) {
        return;
    }
    auto   values = _mv_mapping_read_view.get(docid);
    size_t to_keep = 0;
    for (uint32_t candidate : element_ids) {
        if (candidate >= values.size()) {
            break;
        }
        if (this->matcher().match(multivalue::get_value(values[candidate]))) {
            element_ids[to_keep++] = candidate;
        }
    }
    element_ids.resize(to_keep);
}

} // namespace search::attribute
