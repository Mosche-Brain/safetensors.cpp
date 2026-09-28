/*
 * @author: jaro
 * @name:   basic_usage
 * @file:   modules/safetensors.cpp/examples/basic_usage.cpp
 * @date:   28 September 2026 22:37:20
 */

#include <cum/cum.hpp>
#include <safetensors.hpp>

#include <print>

int main()
{
    cum::cum(cum::DEVICE::CPU); // Initializing cum library

    cum::Tensor tensor(cum::Shape{6, 7, 9}, cum::datatype::FP32);

    for (int i = 0 ; i < 6 ; i++)
        for (int j = 0; j < 7 ; j++)
            for (int k = 0; k < 9 ; k++)
                tensor.at<cum::f32>({i, j, k}) = static_cast<float>(i * j * k);

    safetensors::save_safetensor(tensor, "tensor.safetensors");

    cum::Tensor temsor = safetensors::load_safetensor("tensor.safetensors"); // For files containing one tensor passing ID isn't necessary

    bool are_equal = tensor == temsor;

    are_equal ? std::println("git") : std::println("nie git");

    return 0;
}
