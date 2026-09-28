/*
 * @author: jaro
 * @name:   safetensors
 * @file:   modules/safetensors.cpp/safetensors.cpp
 * @date:   28 September 2026 20:22:03
 */



#define SAFETENSORS_MAX_DIM 8
#define SAFETENSORS_MAX_TENSORS 2048
#define SAFETENSORS_MAX_FILE_SIZE (2ULL << 40) // 2 TiB
#define SAFETENSORS_MAX_STRING_SIZE 2048
#define SAFETENSORS_MAX_METADATA_SIZE 8192

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <string>
#include <unordered_map>
#include <vector>

#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>


// tbh, original version of this code is even stranger than mine, really
// https://github.com/carsonpo/safetensors.cpp

#include "safetensors/safetensors.hpp"

namespace safetensors
{
    inline cum::datatype get_cum_dtype(const std::string& dtype_str)
    {
        static const std::unordered_map<std::string, cum::datatype> dtype_map = {
            {"U8", cum::datatype::U8},
            {"I8", cum::datatype::S8},
            {"U16", cum::datatype::U16},
            {"I16", cum::datatype::S16},
            {"U32", cum::datatype::U32},
            {"I32", cum::datatype::S32},
            {"U64", cum::datatype::U64},
            {"I64", cum::datatype::S64},
            {"F16", cum::datatype::FP16},
            {"BF16", cum::datatype::BF16},
            {"F32", cum::datatype::FP32},
            {"F64", cum::datatype::FP64},
        };

        const auto it = dtype_map.find(dtype_str);
        if (it == dtype_map.end())
            throw std::runtime_error("Unknown dtype: " + dtype_str);

        return it->second;
    }

    inline std::string get_safetensors_dtype(cum::datatype dtype)
    {
        static const std::unordered_map<cum::datatype, std::string> dtype_map = {
            {cum::datatype::U8, "U8"},
            {cum::datatype::S8, "I8"},
            {cum::datatype::U16, "U16"},
            {cum::datatype::S16, "I16"},
            {cum::datatype::U32, "U32"},
            {cum::datatype::S32, "I32"},
            {cum::datatype::U64, "U64"},
            {cum::datatype::S64, "I64"},
            {cum::datatype::FP16, "F16"},
            {cum::datatype::BF16, "BF16"},
            {cum::datatype::FP32, "F32"},
            {cum::datatype::FP64, "F64"},
        };

        const auto it = dtype_map.find(dtype);
        if (it == dtype_map.end())
            throw std::runtime_error("Unsupported dtype");

        return it->second;
    }

    inline bool is_big_endian()
    {
        const std::uint32_t value = 0x01020304;
        const auto* bytes = reinterpret_cast<const std::uint8_t*>(&value);

        return bytes[0] == 0x01;
    }

    template <typename T>
    inline T swap_endian(T value)
    {
        static_assert(CHAR_BIT == 8, "CHAR_BIT != 8");

        T result{};

        const auto* source =
            reinterpret_cast<const std::uint8_t*>(&value);

        auto* destination =
            reinterpret_cast<std::uint8_t*>(&result);

        for (std::size_t i = 0; i < sizeof(T); ++i)
            destination[i] = source[sizeof(T) - i - 1];

        return result;
    }

    inline void validate_string_length(
        const std::string& str,
        const std::string& context)
    {
        if (str.length() > SAFETENSORS_MAX_STRING_SIZE)
            throw std::runtime_error(
                context + " exceeds maximum allowed length");
    }

    class SimpleJSONParser
    {
    private:
        const char* json;
        std::size_t pos;

        inline void skipWhitespace()
        {
            while (
                json[pos] == ' ' ||
                json[pos] == '\n' ||
                json[pos] == '\r' ||
                json[pos] == '\t')
            {
                ++pos;
            }
        }

        inline std::string parseString()
        {
            if (json[pos] != '"')
                throw std::runtime_error("Expected JSON string");

            ++pos;

            std::string result;

            while (json[pos] != '"')
            {
                if (json[pos] == '\\')
                {
                    ++pos;

                    switch (json[pos])
                    {
                        case '"':
                            result += '"';
                            ++pos;
                            break;

                        case '\\':
                            result += '\\';
                            ++pos;
                            break;

                        case '/':
                            result += '/';
                            ++pos;
                            break;

                        case 'b':
                            result += '\b';
                            ++pos;
                            break;

                        case 'f':
                            result += '\f';
                            ++pos;
                            break;

                        case 'n':
                            result += '\n';
                            ++pos;
                            break;

                        case 'r':
                            result += '\r';
                            ++pos;
                            break;

                        case 't':
                            result += '\t';
                            ++pos;
                            break;

                        case 'u':
                            throw std::runtime_error(
                                "Unicode JSON escapes are not supported");

                        default:
                            throw std::runtime_error(
                                "Invalid JSON escape sequence");
                    }

                    continue;
                }

                result += json[pos++];
            }

            ++pos;
            return result;
        }

        inline std::vector<std::int64_t> parseArray()
        {
            if (json[pos] != '[')
                throw std::runtime_error("Expected JSON array");

            ++pos;

            std::vector<std::int64_t> result;

            skipWhitespace();

            if (json[pos] == ']')
            {
                ++pos;
                return result;
            }

            while (true)
            {
                skipWhitespace();

                bool negative = false;

                if (json[pos] == '-')
                {
                    negative = true;
                    ++pos;
                }

                if (!std::isdigit(
                        static_cast<unsigned char>(json[pos])))
                {
                    throw std::runtime_error(
                        "Expected integer in JSON array");
                }

                std::int64_t value = 0;

                while (std::isdigit(
                    static_cast<unsigned char>(json[pos])))
                {
                    value =
                        value * 10 +
                        (json[pos] - '0');

                    ++pos;
                }

                result.push_back(negative ? -value : value);

                skipWhitespace();

                if (json[pos] == ']')
                {
                    ++pos;
                    return result;
                }

                if (json[pos] != ',')
                    throw std::runtime_error(
                        "Expected ',' in JSON array");

                ++pos;
            }
        }

        inline std::array<std::size_t, 2> parseDataOffsets()
        {
            const auto values = parseArray();

            if (values.size() != 2)
                throw std::runtime_error(
                    "data_offsets must contain exactly two values");

            if (values[0] < 0 || values[1] < 0)
                throw std::runtime_error(
                    "data_offsets cannot contain negative values");

            return {
                static_cast<std::size_t>(values[0]),
                static_cast<std::size_t>(values[1])
            };
        }

        inline void skipValue()
        {
            skipWhitespace();

            if (json[pos] == '"')
            {
                parseString();
                return;
            }

            if (json[pos] != '{' && json[pos] != '[')
            {
                while (
                    json[pos] != ',' &&
                    json[pos] != '}' &&
                    json[pos] != '\0')
                {
                    ++pos;
                }

                return;
            }

            const char opening = json[pos];
            const char closing =
                opening == '{' ? '}' : ']';

            int depth = 0;
            bool inside_string = false;
            bool escaped = false;

            while (json[pos] != '\0')
            {
                const char c = json[pos++];

                if (inside_string)
                {
                    if (escaped)
                    {
                        escaped = false;
                    }
                    else if (c == '\\')
                    {
                        escaped = true;
                    }
                    else if (c == '"')
                    {
                        inside_string = false;
                    }

                    continue;
                }

                if (c == '"')
                {
                    inside_string = true;
                }
                else if (c == opening)
                {
                    ++depth;
                }
                else if (c == closing)
                {
                    --depth;

                    if (depth == 0)
                        return;
                }
            }

            throw std::runtime_error(
                "Unexpected end of JSON value");
        }

        inline TensorInfo parseTensorInfo()
        {
            if (json[pos] != '{')
                throw std::runtime_error(
                    "Expected tensor info object");

            ++pos;

            TensorInfo info{};

            while (true)
            {
                skipWhitespace();

                if (json[pos] == '}')
                {
                    ++pos;
                    return info;
                }

                const std::string key = parseString();

                skipWhitespace();

                if (json[pos] != ':')
                    throw std::runtime_error(
                        "Expected ':' after JSON key");

                ++pos;
                skipWhitespace();

                if (key == "dtype")
                {
                    info.dtype = get_cum_dtype(parseString());
                }
                else if (key == "shape")
                {
                    info.shape = parseArray();
                }
                else if (key == "data_offsets")
                {
                    info.data_offsets = parseDataOffsets();
                }
                else
                {
                    skipValue();
                }

                skipWhitespace();

                if (json[pos] == ',')
                {
                    ++pos;
                    continue;
                }

                if (json[pos] == '}')
                {
                    ++pos;
                    return info;
                }

                throw std::runtime_error(
                    "Expected ',' or '}' in tensor info");
            }
        }

    public:
        explicit SimpleJSONParser(const char* json_str)
            : json(json_str), pos(0)
        {
        }

        inline std::unordered_map<std::string, TensorInfo> parse()
        {
            std::unordered_map<std::string, TensorInfo> result;

            skipWhitespace();

            if (json[pos] != '{')
                throw std::runtime_error(
                    "Expected JSON object");

            ++pos;

            while (true)
            {
                skipWhitespace();

                if (json[pos] == '}')
                    return result;

                const std::string key = parseString();

                skipWhitespace();

                if (json[pos] != ':')
                    throw std::runtime_error(
                        "Expected ':' after JSON key");

                ++pos;
                skipWhitespace();

                if (key == "__metadata__")
                {
                    skipValue();
                }
                else
                {
                    result.emplace(
                        key,
                        parseTensorInfo());
                }

                skipWhitespace();

                if (json[pos] == ',')
                {
                    ++pos;
                    continue;
                }

                if (json[pos] == '}')
                    return result;

                throw std::runtime_error(
                    "Expected ',' or '}' in JSON object");
            }
        }
    };

    inline std::uint64_t read_header_size(
        const void* data,
        std::size_t file_size)
    {
        if (file_size < sizeof(std::uint64_t))
            throw std::runtime_error("Invalid file size");

        std::uint64_t header_size{};

        std::memcpy(
            &header_size,
            data,
            sizeof(header_size));

        if (is_big_endian())
            header_size = swap_endian(header_size);

        return header_size;
    }

    inline std::unordered_map<std::string, TensorInfo>
    parse_safetensors_header_info(
        const char* data,
        std::size_t size)
    {
        const std::uint64_t header_size =
            read_header_size(data, size);

        if (
            header_size >
            size - sizeof(std::uint64_t))
        {
            throw std::runtime_error(
                "Invalid header size");
        }

        SimpleJSONParser parser(data + 8);

        return parser.parse();
    }

    inline bool is_byte_swappable_dtype(cum::datatype dtype)
    {
        return
            dtype == cum::datatype::FP16 ||
            dtype == cum::datatype::BF16 ||
            dtype == cum::datatype::FP32 ||
            dtype == cum::datatype::FP64;
    }

    inline void swap_tensor_endian(cum::Tensor& tensor)
    {
        const auto element_size =
            cum::datatype_size(tensor.type());

        if (element_size <= 1)
            return;

        auto* data = tensor.data<char>();

        for (
            cum::dim_t offset = 0;
            offset < tensor.size();
            offset += element_size)
        {
            std::reverse(
                data + offset,
                data + offset + element_size);
        }
    }

    std::unordered_map<std::string, cum::Tensor>
    load_safetensors(const std::string& filename)
    {
        const int fd = open(
            filename.c_str(),
            O_RDONLY);

        if (fd == -1)
        {
            throw std::runtime_error(
                "Failed to open file: " + filename);
        }

        struct stat sb{};

        if (fstat(fd, &sb) == -1)
        {
            close(fd);

            throw std::runtime_error(
                "Failed to get file size");
        }

        const std::size_t file_size =
            static_cast<std::size_t>(sb.st_size);

        if (file_size > SAFETENSORS_MAX_FILE_SIZE)
        {
            close(fd);

            throw std::runtime_error(
                "File size exceeds maximum allowed size");
        }

        void* mapped_file = mmap(
            nullptr,
            file_size,
            PROT_READ,
            MAP_PRIVATE,
            fd,
            0);

        if (mapped_file == MAP_FAILED)
        {
            close(fd);

            throw std::runtime_error(
                "Failed to memory map file");
        }

        try
        {
            const std::uint64_t header_size =
                read_header_size(
                    mapped_file,
                    file_size);

            if (
                header_size >
                file_size - sizeof(std::uint64_t))
            {
                throw std::runtime_error(
                    "Invalid header size");
            }

            const auto tensor_infos =
                parse_safetensors_header_info(
                    static_cast<const char*>(mapped_file),
                    file_size);

            if (tensor_infos.size() >
                SAFETENSORS_MAX_TENSORS)
            {
                throw std::runtime_error(
                    "Number of tensors exceeds maximum allowed");
            }

            const std::size_t data_start_offset =
                sizeof(std::uint64_t) +
                static_cast<std::size_t>(header_size);

            const std::size_t data_size =
                file_size - data_start_offset;

            const auto* data_start =
                static_cast<const char*>(mapped_file) +
                data_start_offset;

            std::unordered_map<std::string, cum::Tensor> tensors;

            tensors.reserve(tensor_infos.size());

            for (const auto& [name, info] : tensor_infos)
            {
                validate_string_length(
                    name,
                    "Tensor name");

                if (info.shape.size() >
                    SAFETENSORS_MAX_DIM)
                {
                    throw std::runtime_error(
                        "Tensor dimension exceeds maximum allowed");
                }

                const std::size_t begin =
                    info.data_offsets[0];

                const std::size_t end =
                    info.data_offsets[1];

                if (begin > end)
                {
                    throw std::runtime_error(
                        "Invalid tensor data offsets");
                }

                if (end > data_size)
                {
                    throw std::runtime_error(
                        "Tensor data offsets exceed file size");
                }

                auto tensor =
                    cum::Tensor::take_memory(
                        info.shape,
                        const_cast<char*>(
                            data_start + begin),
                        info.dtype);

                if (
                    is_big_endian() &&
                    is_byte_swappable_dtype(
                        tensor.type()))
                {
                    swap_tensor_endian(tensor);
                }

                tensors.emplace(
                    name,
                    std::move(tensor));
            }

            munmap(mapped_file, file_size);
            close(fd);

            return tensors;
        }
        catch (...)
        {
            munmap(mapped_file, file_size);
            close(fd);
            throw;
        }
    }

    void save_safetensors(
        const std::unordered_map<std::string, cum::Tensor>& tensors,
        const std::string& filename,
        const std::unordered_map<std::string, std::string>& metadata)
    {
        if (tensors.size() >
            SAFETENSORS_MAX_TENSORS)
        {
            throw std::runtime_error(
                "Number of tensors exceeds maximum allowed");
        }

        std::string header_json = "{";
        std::vector<char> data_buffer;

        std::size_t current_offset = 0;

        if (!metadata.empty())
        {
            header_json += "\"__metadata__\":{";

            bool first = true;

            for (const auto& [key, value] : metadata)
            {
                validate_string_length(
                    key,
                    "Metadata key");

                validate_string_length(
                    value,
                    "Metadata value");

                if (!first)
                    header_json += ",";

                header_json +=
                    "\"" + key + "\":\"" + value + "\"";

                first = false;
            }

            header_json += "},";
        }

        for (const auto& [name, tensor] : tensors)
        {
            validate_string_length(
                name,
                "Tensor name");

            if (tensor.rank() >
                SAFETENSORS_MAX_DIM)
            {
                throw std::runtime_error(
                    "Tensor dimension exceeds maximum allowed");
            }

            auto tensor_clone = tensor.clone();

            if (
                is_big_endian() &&
                is_byte_swappable_dtype(
                    tensor_clone.type()))
            {
                swap_tensor_endian(tensor_clone);
            }

            const auto dtype =
                get_safetensors_dtype(
                    tensor_clone.type());

            const auto shape =
                tensor_clone.shape();

            const std::size_t tensor_size =
                static_cast<std::size_t>(
                    tensor_clone.size());

            if (header_json.length() > 1)
                header_json += ",";

            header_json +=
                "\"" + name + "\":{";

            header_json +=
                "\"dtype\":\"" + dtype + "\",";

            header_json += "\"shape\":[";

            for (std::size_t i = 0;
                 i < shape.size();
                 ++i)
            {
                if (i > 0)
                    header_json += ",";

                header_json +=
                    std::to_string(shape[i]);
            }

            header_json += "],";

            header_json +=
                "\"data_offsets\":[" +
                std::to_string(current_offset) +
                "," +
                std::to_string(
                    current_offset + tensor_size) +
                "]}";

            const char* tensor_data =
                tensor_clone.data<const char>();

            data_buffer.insert(
                data_buffer.end(),
                tensor_data,
                tensor_data + tensor_size);

            current_offset += tensor_size;
        }

        header_json += "}";

        const std::uint64_t header_size =
            static_cast<std::uint64_t>(
                header_json.size());

        if (header_size >
            SAFETENSORS_MAX_METADATA_SIZE)
        {
            throw std::runtime_error(
                "Metadata size exceeds maximum allowed size");
        }

        if (
            sizeof(std::uint64_t) +
            header_size +
            data_buffer.size() >
            SAFETENSORS_MAX_FILE_SIZE)
        {
            throw std::runtime_error(
                "Total file size exceeds maximum allowed size");
        }

        std::ofstream file(
            filename,
            std::ios::binary);

        if (!file)
        {
            throw std::runtime_error(
                "Failed to open file for writing: " +
                filename);
        }

        std::uint64_t stored_header_size =
            header_size;

        if (is_big_endian())
            stored_header_size =
                swap_endian(stored_header_size);

        file.write(
            reinterpret_cast<const char*>(
                &stored_header_size),
            sizeof(stored_header_size));

        file.write(
            header_json.data(),
            static_cast<std::streamsize>(
                header_json.size()));

        if (!data_buffer.empty())
        {
            file.write(
                data_buffer.data(),
                static_cast<std::streamsize>(
                    data_buffer.size()));
        }

        if (!file)
        {
            throw std::runtime_error(
                "Failed to write file: " +
                filename);
        }
    }

}