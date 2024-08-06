// ReSharper disable CppInconsistentNaming
#pragma once
#include <vector>
#include <string>
#include <cstring>

namespace nefarius::util
{
    // Template class for double-NULL-terminated multi-string array
    template <typename CharT>
    class MultiStringArray
    {
    public:
        using StringType = std::basic_string<CharT>;
        using CharType = CharT;

        MultiStringArray() = default;

        // Construct from a vector of strings
        explicit MultiStringArray(const std::vector<StringType>& strings)
        {
            from_vector(strings);
        }

        // Convert to a vector of strings
        std::vector<StringType> to_vector() const
        {
            std::vector<StringType> result;
            const CharType* p = data_.data();
            while (*p)
            {
                result.emplace_back(p);
                p += std::char_traits<CharType>::length(p) + 1;
            }
            return result;
        }

        // Initialize from a vector of strings
        void from_vector(const std::vector<StringType>& strings)
        {
            size_t total_length = 0;
            for (const auto& str : strings)
            {
                total_length += str.size() + 1;
            }
            total_length++; // For the final double-NULL termination

            data_.resize(total_length);
            CharType* p = data_.data();
            for (const auto& str : strings)
            {
                std::memcpy(p, str.data(), str.size() * sizeof(CharType));
                p += str.size();
                *p++ = CharType('\0');
            }
            *p = CharType('\0');
        }

        // Get the raw data
        const CharType* c_str() const
        {
            return data_.data();
        }

        // Get the size of the raw data
        [[nodiscard]] size_t size() const
        {
            return data_.size();
        }

    private:
        std::vector<CharType> data_;
    };

    // Type aliases for narrow and wide versions
    using NarrowMultiStringArray = MultiStringArray<char>;
    using WideMultiStringArray = MultiStringArray<wchar_t>;
}
