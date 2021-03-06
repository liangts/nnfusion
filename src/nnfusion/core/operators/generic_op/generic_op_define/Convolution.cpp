// Copyright (c) Microsoft Corporation.
// Licensed under the MIT License.

#include "nnfusion/core/operators/generic_op/generic_op.hpp"

REGISTER_OP(Convolution)
    .infershape(nnfusion::op::infershape::unimplemented_and_not_used)
    .translate([](std::shared_ptr<graph::GNode> curr) -> std::string {
        auto _op = static_pointer_cast<nnfusion::op::Convolution>(curr->get_op_ptr());
        NNFUSION_CHECK_NOT_NULLPTR(_op) << "Node type is not " << curr->get_op_ptr()->get_op_type();

        const auto& dilation = _op->get_window_dilation_strides();
        const auto& stride = _op->get_window_movement_strides();
        const auto& padding_below = _op->get_padding_below();
        const auto& padding_above = _op->get_padding_above();
        const auto& data_format = _op->get_data_format();
        int64_t padding[] = {
            padding_below[1], padding_below[0], padding_above[1], padding_above[0]};

        return op::create_code_from_template(
            R"( - input("input0", @input_shape_0@); input("input1", @input_shape_1@); output(@output_shape@, topi=topi.nn.conv2d_@data_format@(args("input0"), args("input1"), stride=@stride@, padding=@padding@, dilation=@dilation@)); )",
            {{"input_shape_0", vector_to_string(curr->get_input_shape(0))},
             {"input_shape_1", vector_to_string(curr->get_input_shape(1))},
             {"output_shape", vector_to_string(curr->get_output_shape(0))},
             {"data_format", data_format == "NCHW" ? "nchw" : "nhwc"},
             {"stride", vector_to_string(stride)},
             {"padding", vector_to_string(padding)},
             {"dilation", vector_to_string(dilation)}});
    })
    .translate_v2([](std::shared_ptr<graph::GNode> curr) -> std::string {
        auto ir_template =
            R"( @output0@@output0_layout@ +=! @input0@@input0_layout@ * @input1@@input1_layout@@pad_cond@ where HO in @height@, WO in @width@; )";
        auto manual_rule = R"( ## @: plan/convfwd_@data_format@_v1 )";

        auto _op = static_pointer_cast<nnfusion::op::Convolution>(curr->get_op_ptr());
        NNFUSION_CHECK_NOT_NULLPTR(_op) << "Node type is not " << curr->get_op_ptr()->get_op_type();

        const auto& is_nchw = _op->get_data_format() == "NCHW";
        const auto& padding_h = _op->get_padding_below()[0];
        const auto& padding_w = _op->get_padding_below()[1];
        const auto& in_shape = curr->get_input_shape(0);
        const std::string data_format = is_nchw ? "nchw" : "nhwc";

        nnfusion::op::OpConfig::any config;
        auto shape_template = is_nchw ? "[N, C, -@pad_0@ + HO + KH, -@pad_1@ + WO + KW]"
                                      : "[N, -@pad_0@ + HO + KH, -@pad_1@ + WO + KW, C]";
        config["input1_layout"] = "[KH, KW, C, F]";
        config["output0_layout"] = is_nchw ? "[N, F, HO, WO]" : "[N, HO, WO, F]";
        config["height"] = is_nchw ? in_shape[2] : in_shape[1];
        config["width"] = is_nchw ? in_shape[3] : in_shape[2];
        config["pad_0"] = to_string(padding_h);
        config["pad_1"] = to_string(padding_w);
        config["input0_layout"] = op::create_code_from_template(shape_template, config);

        std::string pad_cond;
        if (padding_h || padding_w)
        {
            auto pad_template =
                ".when([-@pad_0@ + HO + KH >= 0, -@pad_0@ + HO + KH < @height@, -@pad_1@ + WO + KW "
                ">= 0, -@pad_1@ + WO + KW < @width@], 0.0)";
            pad_cond = op::create_code_from_template(pad_template, config);
        }
        config["pad_cond"] = pad_cond;

        return op::create_code_from_template(ir_template, config) +
               op::create_code_from_template(manual_rule, {{"data_format", data_format}});
    });
