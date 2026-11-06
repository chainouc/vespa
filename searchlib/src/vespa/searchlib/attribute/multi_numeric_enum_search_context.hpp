// Copyright Vespa.ai. Licensed under the terms of the Apache 2.0 license. See LICENSE in the project root.

#pragma once

#include "multi_enum_search_context.h"
#include "multi_numeric_enum_search_context.h"

namespace search::attribute {

template <typename T, typename M>
MultiNumericEnumSearchContext<T, M>::MultiNumericEnumSearchContext(std::unique_ptr<QueryTermSimple> qTerm,
                                                                   const AttributeVector&           toBeSearched,
                                                                   MultiValueMappingReadView<M> mv_mapping_read_view,
                                                                   const EnumStoreT<T>&         enum_store)
    : MultiEnumSearchContext<T, NumericSearchContext<NumericRangeMatcher<T>>, M>(
          NumericRangeMatcher<T>(*qTerm), toBeSearched, mv_mapping_read_view, enum_store) {
}

template <typename T, typename M> MultiNumericEnumSearchContext<T, M>::~MultiNumericEnumSearchContext() = default;

template <typename T, typename M>
void MultiNumericEnumSearchContext<T, M>::and_element_ids_into(uint32_t               docid,
                                                               std::vector<uint32_t>& element_ids) const {
    // Keep find-next skipping for larger candidate sets.
    if (element_ids.size() > 2) {
        ISearchContext::and_element_ids_into(docid, element_ids);
        return;
    }
    if (element_ids.empty()) {
        return;
    }
    auto   values = this->_mv_mapping_read_view.get(docid);
    size_t to_keep = 0;
    for (uint32_t candidate : element_ids) {
        if (candidate >= values.size()) {
            break;
        }
        T value = this->_enum_store.get_value(multivalue::get_value_ref(values[candidate]).load_acquire());
        if (this->matcher().match(value)) {
            element_ids[to_keep++] = candidate;
        }
    }
    element_ids.resize(to_keep);
}

} // namespace search::attribute
