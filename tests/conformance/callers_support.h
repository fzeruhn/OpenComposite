#pragma once
// Support for the generated callers (gen_callers.py): the argument makers for each argument set, and the recording of
// what a call returned and wrote. Included only by the generated file and oc_conformance.

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifdef _WIN32
#include <d3d11.h>
#endif

// Types Valve deleted from OpenVR that old headers still use (as stdafx.h does for OpenLamina)
#include "custom_types.h"
#include "interfaces/vrtypes.h"

#include <json/json.h>

#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

enum class ArgSet { Valid, Edge, Null, Invalid };

struct CallContext {
#ifdef _WIN32
	ID3D11Device* d3d11 = nullptr;
#else
	void* d3d11 = nullptr;
#endif
	bool againstRealRuntime = false;
	std::filesystem::path dataDir;
};

struct CallRecord {
	std::string method;
	int index = 0;
	Json::Value ret;
	Json::Value out{ Json::objectValue };
	std::string skipped; // "unsafe" when not called against a real runtime
};

using CallerFn = CallRecord (*)(void* iface, ArgSet set, CallContext& ctx);

// The table for an interface version, nullptr if no header has it
const std::vector<CallerFn>* FindCallers(const char* interfaceVersion);
std::vector<std::string> GeneratedVersions();

namespace callers {

inline std::string Hex(const void* data, size_t size)
{
	static const char digits[] = "0123456789abcdef";
	std::string s;
	s.reserve(size * 2);
	auto* b = (const uint8_t*)data;
	for (size_t i = 0; i < size; i++) {
		s += digits[b[i] >> 4];
		s += digits[b[i] & 15];
	}
	return s;
}

// A value of T to pass as "valid" for types where all-zero isn't
template <typename T>
inline void MakeValid(T& v)
{
	std::memset((void*)&v, 0, sizeof(T));
}
template <>
inline void MakeValid(vr::HmdMatrix34_t& m)
{
	std::memset(&m, 0, sizeof(m));
	m.m[0][0] = m.m[1][1] = m.m[2][2] = 1;
}
template <>
inline void MakeValid(vr::VRTextureBounds_t& b)
{
	b = { 0, 0, 1, 1 };
}

// --- recording ---

template <typename T>
Json::Value Record(const T& v)
{
	if constexpr (std::is_same_v<T, bool>)
		return Json::Value(v);
	else if constexpr (std::is_enum_v<T>)
		return Json::Value((Json::Int64)v);
	else if constexpr (std::is_floating_point_v<T>)
		return std::isfinite(v) ? Json::Value((double)v) : Json::Value("non-finite");
	else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>)
		return Json::Value((Json::Int64)v);
	else if constexpr (std::is_integral_v<T>)
		return Json::Value((Json::UInt64)v);
	else if constexpr (std::is_same_v<T, const char*> || std::is_same_v<T, char*>)
		return v ? Json::Value(std::string(v)) : Json::Value("null");
	else if constexpr (std::is_pointer_v<T>)
		return Json::Value(v ? "non-null" : "null");
	else
		return Json::Value(Hex(&v, sizeof(T)));
}

// --- argument makers ---

// Integers, enums, floats and bools by value
template <typename T>
struct Scalar {
	T v{};
	Scalar(ArgSet set)
	{
		if constexpr (std::is_same_v<T, bool>) {
			v = set == ArgSet::Valid || set == ArgSet::Invalid;
		} else if constexpr (std::is_floating_point_v<T>) {
			v = set == ArgSet::Valid ? (T)0.5 : set == ArgSet::Invalid ? std::numeric_limits<T>::quiet_NaN() : (T)0;
		} else if constexpr (std::is_enum_v<T>) {
			using U = std::underlying_type_t<T>;
			v = set == ArgSet::Edge ? (T)std::numeric_limits<U>::max() : set == ArgSet::Invalid ? (T)0x7fff : (T)0;
		} else {
			v = set == ArgSet::Edge ? std::numeric_limits<T>::max() : (T)0;
		}
	}
	T get() const { return v; }
};

// Handles (VROverlayHandle_t and the like, typedefs of integers)
template <typename T>
struct Handle {
	T v{};
	Handle(ArgSet set)
	{
		uint64_t raw = set == ArgSet::Valid ? 1 : set == ArgSet::Invalid ? 0xDEADBEEF : 0;
		if constexpr (std::is_pointer_v<T>)
			v = reinterpret_cast<T>((uintptr_t)raw);
		else
			v = (T)raw;
	}
	T get() const { return v; }
};

// Input strings
struct String {
	std::string s;
	const char* p = nullptr;
	String(ArgSet set, const char* name, CallContext& ctx)
	{
		std::string n = name;
		switch (set) {
		case ArgSet::Null:
			p = nullptr;
			return;
		case ArgSet::Edge:
			s = "";
			break;
		case ArgSet::Invalid:
			s = std::string(4096, 'x');
			break;
		case ArgSet::Valid:
			if (n == "pchSection")
				s = "steamvr";
			else if (n == "pchSettingsKey")
				s = "supersampleScale";
			else if (n == "pchAppKey" || n == "pchTemplateAppKey" || n == "pchNewAppKey")
				s = "oc.conformance.app";
			else if (n.find("Manifest") != std::string::npos)
				s = (ctx.dataDir / "manifest_basic.json").string();
			else if (n == "pchActionName" || n == "pchActionSetName")
				s = n == "pchActionName" ? "/actions/main/in/trigger" : "/actions/main";
			else if (n == "pchInputSourcePath")
				s = "/user/hand/right";
			else
				s = "test";
			break;
		}
		p = s.c_str();
	}
	const char* get() const { return p; }
};

// Pointers (out or in), alone or with a size/count argument after them
enum class PtrMode { Single, Bytes, Count, BytesAndCount };

template <typename T>
struct Ptr {
	using Elem = std::remove_const_t<T>;
	static constexpr size_t ElemSize = std::is_void_v<Elem> ? 1 : sizeof(std::conditional_t<std::is_void_v<Elem>, char, Elem>);
	std::vector<uint8_t> storage;
	T* p = nullptr;
	uint32_t count = 0;
	uint32_t bytes = 0;

	Ptr(ArgSet set, PtrMode mode)
	{
		uint32_t elems = mode == PtrMode::Single ? 1 : 16;
		if constexpr (std::is_void_v<Elem> || std::is_same_v<Elem, char>)
			elems = mode == PtrMode::Single ? 1024 : 1024;
		if (set == ArgSet::Edge && mode != PtrMode::Single)
			elems = 1;
		storage.assign(std::max<size_t>(1, elems * ElemSize) + 64, 0);
		if constexpr (!std::is_void_v<Elem> && !std::is_same_v<Elem, char>) {
			if (set == ArgSet::Valid || set == ArgSet::Edge)
				for (uint32_t i = 0; i < elems; i++)
					MakeValid(((Elem*)storage.data())[i]);
		}
		count = elems;
		bytes = (uint32_t)(elems * ElemSize);
		if (set == ArgSet::Edge && mode == PtrMode::Bytes && !std::is_same_v<Elem, char> && !std::is_void_v<Elem>)
			bytes = (uint32_t)(ElemSize >= 4 ? ElemSize - 4 : ElemSize); // a struct 4 bytes short
		p = (T*)storage.data();
		if (set == ArgSet::Null) {
			p = nullptr;
			count = mode == PtrMode::Single ? 0 : 0;
		}
		if (set == ArgSet::Invalid) {
			if (mode == PtrMode::Single || std::is_same_v<Elem, char> || std::is_void_v<Elem>)
				p = nullptr; // a null buffer with a non-zero size
			bytes = mode == PtrMode::Bytes && !std::is_same_v<Elem, char> && !std::is_void_v<Elem> ? 0 : bytes;
		}
	}
	T* get() const { return p; }
	template <typename U>
	U size_as() const { return (U)bytes; }
	template <typename U>
	U count_as() const { return (U)count; }

	Json::Value recorded() const
	{
		if (!p)
			return Json::Value("null");
		if constexpr (std::is_same_v<Elem, char>)
			return Json::Value(std::string((const char*)storage.data(), strnlen((const char*)storage.data(), storage.size())));
		else if constexpr (std::is_void_v<Elem>)
			return Json::Value(Hex(storage.data(), std::min<size_t>(64, storage.size())));
		else if constexpr (std::is_arithmetic_v<Elem> || std::is_enum_v<Elem>)
			return Record(*(const Elem*)storage.data());
		else
			return Json::Value(Hex(storage.data(), std::min<size_t>(ElemSize * std::max<uint32_t>(1, count == 16 ? 1 : count), 512)));
	}
};

// Pointers to objects of other APIs (Vulkan, D3D) that can't be made here
template <typename T>
struct Opaque {
	T* p = nullptr;
	Opaque(ArgSet set) { p = set == ArgSet::Invalid ? (T*)(uintptr_t)1 : nullptr; }
	T* get() const { return p; }
};

// T** out-pointers (the callee hands back a pointer)
template <typename T>
struct PtrPtr {
	T* value = nullptr;
	T** p = nullptr;
	PtrPtr(ArgSet set) { p = set == ArgSet::Null ? nullptr : &value; }
	T** get() const { return p; }
	Json::Value recorded() const { return Json::Value(value ? "non-null" : "null"); }
};

// Structs and unions by value
template <typename T>
struct ByValue {
	T v{};
	ByValue(ArgSet set)
	{
		std::memset((void*)&v, 0, sizeof(T));
		if (set == ArgSet::Valid)
			MakeValid(v);
	}
	T get() const { return v; }
};

// A by-value argument: Scalar for integers, enums, floats and bools, ByValue for structs and unions
template <typename T>
using Value = std::conditional_t<std::is_class_v<T> || std::is_union_v<T>, ByValue<T>, Scalar<T>>;

// Texture_t pointers: a real D3D11 texture when the context has a device
struct Texture {
	vr::Texture_t tex{};
	std::shared_ptr<void> owner;
	const vr::Texture_t* p = nullptr;
	Texture(ArgSet set, CallContext& ctx);
	const vr::Texture_t* get() const { return p; }
};

} // namespace callers
