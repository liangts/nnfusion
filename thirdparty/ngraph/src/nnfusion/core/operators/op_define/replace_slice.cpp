//*****************************************************************************
// Copyright 2017-2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************


// Microsoft (c) 2019, NNFusion Team

#include "replace_slice.hpp"
#include "nnfusion/core/graph/gnode.hpp"

using namespace std;
using namespace nnfusion::op;

ReplaceSlice::ReplaceSlice(const nnfusion::Coordinate& lower_bounds,
                           const nnfusion::Coordinate& upper_bounds,
                           const nnfusion::Strides& strides)
    : Op("ReplaceSlice")
    , m_lower_bounds(lower_bounds)
    , m_upper_bounds(upper_bounds)
    , m_strides(strides)
{
}

ReplaceSlice::ReplaceSlice(const nnfusion::Coordinate& lower_bounds,
                           const nnfusion::Coordinate& upper_bounds)
    : Op("ReplaceSlice")
    , m_lower_bounds(lower_bounds)
    , m_upper_bounds(upper_bounds)
    , m_strides(Strides(lower_bounds.size(), 1))
{
}

void ReplaceSlice::validate_and_infer_types(std::shared_ptr<graph::GNode> gnode)
{
    // An empty stride vector with lower_bounds/upper_bounds filled in means that we need to
    // construct the default value.
    if (m_strides.size() == 0)
    {
        m_strides = nnfusion::Strides(m_lower_bounds.size(), 1);
    }

    const nnfusion::PartialShape& arg0_shape = gnode->get_input_partial_shape(0);
    const nnfusion::PartialShape& arg1_shape = gnode->get_input_partial_shape(1);
    nnfusion::Dimension merged_args_rank;

    OP_VALIDATION(
        this, nnfusion::Dimension::merge(merged_args_rank, arg0_shape.rank(), arg1_shape.rank()))
        << "Argument ranks do not match (arg0 shape: " << arg0_shape
        << ", arg1 shape: " << arg1_shape << ").";

    nnfusion::element::Type arg0_et = gnode->get_input_element_type(0);
    nnfusion::element::Type arg1_et = gnode->get_input_element_type(1);
    nnfusion::element::Type merged_args_et;

    OP_VALIDATION(this, nnfusion::element::Type::merge(merged_args_et, arg0_et, arg1_et))
        << "Argument element types do not match (arg0 element type: " << arg0_et
        << ", arg1 element type: " << arg1_et << ").";

    OP_VALIDATION(this,
                  m_lower_bounds.size() == m_upper_bounds.size() &&
                      m_lower_bounds.size() == m_strides.size())
        << "Ranks of lower bounds (" << m_lower_bounds << "), upper bounds (" << m_upper_bounds
        << ") and strides (" << m_strides << ") do not match.";

    size_t output_rank = m_upper_bounds.size();

    for (size_t i = 0; i < output_rank; i++)
    {
        OP_VALIDATION(this, m_lower_bounds[i] <= m_upper_bounds[i])
            << "Lower bound for slice is greater than upper bound at axis " << i
            << " (lower bounds: " << m_lower_bounds << ", upper bounds: " << m_upper_bounds << ").";

        OP_VALIDATION(this, m_strides[i] != 0) << "Stride for slice is zero at axis " << i
                                               << " (strides: " << m_strides << ").";
    }

    OP_VALIDATION(this, merged_args_rank.is_dynamic() || size_t(merged_args_rank) == output_rank)
        << "Argument ranks do not match the rank of the lower bounds (" << m_lower_bounds
        << "), upper bounds (" << m_upper_bounds << "), and strides (" << m_strides << ").";

    std::vector<nnfusion::Dimension> sliced_dims(output_rank);

    for (size_t i = 0; i < output_rank; i++)
    {
        OP_VALIDATION(this,
                      arg0_shape.rank().is_dynamic() || arg0_shape[i].is_dynamic() ||
                          m_upper_bounds[i] <= size_t(arg0_shape[i]))
            << "Upper bound for slice at axis " << i << " is out of range "
            << "(upper bounds: " << m_upper_bounds << ", argument shape: " << arg0_shape << ").";

        size_t sliced_dim = m_upper_bounds[i] - m_lower_bounds[i];
        sliced_dim = sliced_dim / m_strides[i] + ((sliced_dim % m_strides[i] == 0) ? 0 : 1);
        sliced_dims[i] = sliced_dim;
    }

    nnfusion::PartialShape slice_shape{sliced_dims};

    OP_VALIDATION(this, arg1_shape.compatible(slice_shape))
        << "Shape of replacement tensor (" << arg1_shape << ") does not match the slice shape "
        << "(" << slice_shape << ").";

    // Slight corner case here: if arg0 was rank-unknown, we can go ahead and set the output rank
    // because the attribs will have given us enough info.
    nnfusion::PartialShape result_shape =
        (arg0_shape.rank().is_static()) ? arg0_shape
                                        : nnfusion::PartialShape(std::vector<nnfusion::Dimension>(
                                              output_rank, nnfusion::Dimension::dynamic()));

    gnode->set_output_type_and_shape(0, merged_args_et, result_shape);
}
