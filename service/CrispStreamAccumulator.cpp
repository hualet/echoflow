// SPDX-FileCopyrightText: 2026 Hualet Wang
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CrispStreamAccumulator.h"

#include <cctype>

namespace echoflow {

namespace {

// Extract the value of a string field from a compact JSON object.
// Returns nullopt if the field is absent. Handles \" \\ \/ \n \r \t and \uXXXX.
std::optional<std::string> extractJsonString(std::string_view json, std::string_view field)
{
    std::string needle = "\"" + std::string(field) + "\":\"";
    auto pos = json.find(needle);
    if (pos == std::string_view::npos) {
        return std::nullopt;
    }
    pos += needle.size();
    std::string out;
    while (pos < json.size()) {
        char c = json[pos];
        if (c == '"') {
            return out;
        }
        if (c == '\\' && pos + 1 < json.size()) {
            char e = json[pos + 1];
            pos += 2;
            switch (e) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (pos + 4 <= json.size()) {
                        auto hex = std::string(json.substr(pos, 4));
                        try {
                            unsigned cp = static_cast<unsigned>(std::stoul(hex, nullptr, 16));
                            if (cp < 0x80) {
                                out += static_cast<char>(cp);
                            } else if (cp < 0x800) {
                                out += static_cast<char>(0xC0 | (cp >> 6));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            } else {
                                out += static_cast<char>(0xE0 | (cp >> 12));
                                out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                out += static_cast<char>(0x80 | (cp & 0x3F));
                            }
                            pos += 4;
                        } catch (...) {
                            out += e;
                        }
                    } else {
                        out += e;
                    }
                    break;
                }
                default: out += e; break;
            }
        } else {
            out += c;
            ++pos;
        }
    }
    return out;
}

bool isAsciiWordByte(unsigned char b)
{
    return std::isalnum(b) || b == '_' || b == '-' || b == '\'';
}

}  // namespace

bool CrispStreamAccumulator::needsSpace(const std::string& prev, const std::string& next)
{
    if (prev.empty() || next.empty()) {
        return false;
    }
    unsigned char last = static_cast<unsigned char>(prev.back());
    unsigned char first = static_cast<unsigned char>(next.front());
    return isAsciiWordByte(last) && isAsciiWordByte(first);
}

std::optional<std::string> CrispStreamAccumulator::processEvent(std::string_view jsonLine)
{
    auto type = extractJsonString(jsonLine, "type");
    if (!type) {
        return std::nullopt;
    }
    if (*type == "partial") {
        auto text = extractJsonString(jsonLine, "text");
        currentPartial_ = text.value_or("");
        std::string out = finalized_;
        if (needsSpace(out, currentPartial_)) {
            out += " ";
        }
        out += currentPartial_;
        return out;
    }
    if (*type == "final") {
        auto text = extractJsonString(jsonLine, "text");
        currentPartial_.clear();
        std::string piece = text.value_or("");
        if (needsSpace(finalized_, piece)) {
            finalized_ += " ";
        }
        finalized_ += piece;
        return finalized_;
    }
    return std::nullopt;  // silence / unknown
}

std::string CrispStreamAccumulator::finalText() const
{
    std::string out = finalized_;
    if (!currentPartial_.empty()) {
        if (needsSpace(out, currentPartial_)) {
            out += " ";
        }
        out += currentPartial_;
    }
    return out;
}

void CrispStreamAccumulator::clear()
{
    finalized_.clear();
    currentPartial_.clear();
}

}  // namespace echoflow
