#pragma once
// 基础类型别名，对应 LibreOffice 的 sal/types 系列
// 简化版：只保留排版引擎需要的类型

#include <cstdint>
#include <cstddef>
#include <climits>  // 用于 LONG_MAX 等常量

// 对应 sal_Int32 / sal_uInt32 等
using sal_Int8 = int8_t;
using sal_uInt8 = uint8_t;
using sal_Int16 = int16_t;
using sal_uInt16 = uint16_t;
using sal_Int32 = int32_t;
using sal_uInt32 = uint32_t;
using sal_Int64 = int64_t;
using sal_uInt64 = uint64_t;

// LibreOffice 遗留类型别名
using sal_uLong = sal_uInt32;  // 新增：用于 SwFlyCache 等类

// LibreOffice 中 SwTwips 是 tools/Long 的别名，表示 twip 单位（1/20 点，1/1440 英寸）
using SwTwips = sal_Int32;

// SwNodeOffset: 节点数组中的偏移量
using SwNodeOffset = sal_Int32;

// 常量
constexpr SwTwips TWIPS_PER_INCH = 1440;
constexpr SwTwips TWIPS_PER_CM = 567; // 近似值
constexpr SwTwips TWIPS_PER_PT = 20;

// EMU (English Metric Unit) 转换
constexpr double EMU_PER_TWIP = 635.0; // 1 twip = 635 EMU
constexpr double EMU_PER_PT = 12700.0;

// 默认 DPI
constexpr int DEFAULT_DPI = 96;
