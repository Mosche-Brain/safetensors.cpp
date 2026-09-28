//  Copyright (c) 2024 - Present, Carson Poole (?)

#pragma once

#include <unordered_map>

#include <cum/datatypes.hpp>
#include <cum/Tensor.hpp>

namespace safetensors
{
    struct TensorInfo
    {
        cum::datatype dtype;
        cum::Shape shape;
        std::array<size_t, 2> data_offsets;
    };

    using SafeTensors = std::unordered_map<std::string, cum::Tensor>;

    std::unordered_map<std::string, cum::Tensor> load_safetensors(const std::string &filename);

    cum::Tensor load_safetensor(const std::string& filename, const std::string& id);
    cum::Tensor load_safetensor(const std::string& filename); // For files with one tensor

    void save_safetensors(const std::unordered_map<std::string, cum::Tensor> &tensors, const std::string &filename, const std::unordered_map<std::string, std::string> &metadata = {});
    void save_safetensor(const cum::Tensor &tensors, const std::string &filename, const std::string& metadata = {});

}

