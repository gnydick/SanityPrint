#include "HybridVertexBuffer.hpp"

namespace Slic3r {
	

HybridVertexVector::HybridVertexVector(bool use_short_type) 
	:m_use_short_type(use_short_type) 
{}

HybridVertexVector::~HybridVertexVector() {}

HybridVertexVector::HybridVertexVector(HybridVertexVector&& other) 
: m_use_short_type(other.m_use_short_type)
, m_vector_f(std::move(other.m_vector_f))
, m_vector_s(std::move(other.m_vector_s))
{
}

HybridVertexVector::HybridVertexVector(const HybridVertexVector& other)
: m_use_short_type(other.m_use_short_type)
, m_vector_f(other.m_vector_f)
, m_vector_s(other.m_vector_s)
{
}

HybridVertexVector& HybridVertexVector::operator=(const HybridVertexVector& other)
{
    if (this != &other) {
        if (m_use_short_type == other.m_use_short_type) {
            m_vector_f = other.m_vector_f;
            m_vector_s = other.m_vector_s;
        }
    }
    return *this;
}

HybridVertexVector& HybridVertexVector::operator=(HybridVertexVector&& other)
{
    if (this != &other) {
        if (m_use_short_type == other.m_use_short_type) {
            m_vector_f = std::move(other.m_vector_f);
            m_vector_s = std::move(other.m_vector_s);
        }
    }
    return *this;
}

HybridVertexVector::HybridVertexVector(unsigned int count) 
: m_use_short_type(false) 
{
	m_vector_f.resize(count); 
}

size_t HybridVertexVector::size() const 
{ 
	if (m_use_short_type) {
		return m_vector_s.size();
    } else {
        return m_vector_f.size();
    }
}
size_t HybridVertexVector::capacity() const 
{
    if (m_use_short_type) {
        return m_vector_s.capacity();
    } else {
        return m_vector_f.capacity();
    }
}

float HybridVertexVector::operator[](const size_t _Pos) const
{
    if (m_use_short_type) {
        return m_vector_s.operator[](_Pos);
    } else {
        return m_vector_f.operator[](_Pos);
    }
}

size_t HybridVertexVector::size_of_element_type() const
{
    if (m_use_short_type) {
        return sizeof(short);
    } else {
        return sizeof(float);
    }
}

void HybridVertexVector::store_data_at_index(const size_t _Pos, const float& _Val) 
{
    if (m_use_short_type) {
        if (0 <= _Pos && _Pos < m_vector_s.size()) {
            m_vector_s[_Pos] = _Val;
        }
    } else {
        if (0 <= _Pos && _Pos < m_vector_f.size()) {
            m_vector_f[_Pos] = _Val;
        }
    }
}

//void HybridVertexVector::store_data_at_index(const size_t _Pos, const short& _Val) 
//{
//    if (0 <= _Pos && _Pos < m_vector_s.size()) {
//        m_vector_s[_Pos] = _Val;
//    }
//}

//float& HybridVertexVector::operator[](const size_t _Pos)
//{
//    if (m_use_short_type) {
//        static float dummy = 0;
//        return dummy;
//    } else {
//        return m_vector_f.operator[](_Pos);
//    }
//}

//void HybridVertexVector::push_back(const short& _Val) 
//{
//    if (m_use_short_type) {
//        m_vector_s.push_back(_Val);
//    } else {
//        m_vector_f.push_back(static_cast<float>(_Val));
//    }
//}

void HybridVertexVector::push_back(const float& _Val)
{
    if (m_use_short_type) {
        m_vector_s.push_back(static_cast<short>(_Val));
    } else {
        m_vector_f.push_back(_Val);
    }
}

void HybridVertexVector::emplace_back(const unsigned short& _Val)
{
    if (m_use_short_type) {
        m_vector_s.emplace_back(_Val);
    } else {
        m_vector_f.emplace_back(static_cast<float>(_Val));
    }
}

void HybridVertexVector::shrink_to_fit() 
{
    if (m_use_short_type) {
        return m_vector_s.shrink_to_fit();
    } else {
        return m_vector_f.shrink_to_fit();
    }
}

const void* HybridVertexVector::data() const { 
    if (m_use_short_type) {
        return reinterpret_cast<const void*>(m_vector_s.data());
    } else {
        return reinterpret_cast<const void*>(m_vector_f.data());
    }
}

bool HybridVertexVector::empty() const
{
    if (m_use_short_type) {
        return m_vector_s.empty();
    } else {
        return m_vector_f.empty();
    }
}

#if 0

void HybridVertexVector::reserve(size_t count)
{
    if (m_use_short_type) {
        m_vector_s.reserve(count);
    } else {
        m_vector_f.reserve(count);
    }
}

float HybridVertexVector::front() const
{
    if (m_use_short_type) {
        return m_vector_s.front();
    } else {
        return m_vector_f.front();
    }
}

float& HybridVertexVector::front()
{
    if (m_use_short_type) {
        // when the non-const front() method is called in short-type mode, return a reference to a static float
        static float dummy = 0.0f;
        dummy = m_vector_s.front();
        return dummy;
    } else {
        return m_vector_f.front();
    }
}

float HybridVertexVector::back() const
{
    if (m_use_short_type) {
        return m_vector_s.back();
    } else {
        return m_vector_f.back();
    }
}

float& HybridVertexVector::back()
{
    if (m_use_short_type) {
        // when the non-const back() method is called in short-type mode, return a reference to a static float
        static float dummy = 0.0f;
        dummy = m_vector_s.back();
        return dummy;
    } else {
        return m_vector_f.back();
    }
}

void HybridVertexVector::clear() 
{
    if (m_use_short_type) {
        return m_vector_s.clear();
    } else {
        return m_vector_f.clear();
    }
}
#endif

}
