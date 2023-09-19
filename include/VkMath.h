#pragma once

namespace VkMath
{

	static constexpr float PI = 3.14159265;
	static constexpr float INV_PI = 1.0 / PI;
	static constexpr float DEG2RAD_PI = PI / 180.0;
	static constexpr float RAD2DEG_PI = 180.0 / PI;
	
	static inline float Deg2Rad(float deg)
	{
		return deg * DEG2RAD_PI;
	}

	static inline float Rad2Deg(float rad)
	{
		return rad * RAD2DEG_PI;
	}

	// -----------------------------------------------------------------------------
	// Vec2

	struct Vec2
	{
		Vec2() = default;
		Vec2(float x, float y)
			: x(x), y(y) {}
		Vec2(float scalar)
			: x(scalar), y(scalar) {}
		
		union
		{
			float xy[2] = { 0 };
			struct
			{
				float x, y;
			};
		};
	};

	static inline Vec2 Vec2Add(const Vec2& v1, const Vec2& v2)
	{
		return Vec2(v1.x + v2.x, v1.y + v2.y);
	}

	static inline Vec2 Vec2Sub(const Vec2& v1, const Vec2& v2)
	{
		return Vec2(v1.x - v2.x, v1.y - v2.y);
	}

	static inline Vec2 Vec2MulScalar(const Vec2& v, float s)
	{
		return Vec2(v.x * s, v.y * s);
	}

	static inline float Vec2Dot(const Vec2& v1, const Vec2& v2)
	{
		return v1.x * v2.x + v1.y * v2.y;
	}

	static inline float Vec2Length(const Vec2& v)
	{
		return Vec2Dot(v, v);
	}

	static inline Vec2 Vec2Normalize(const Vec2& v)
	{
		float length = Vec2Length(v);
		float rcp_length = 1.0 / length;
		return Vec2MulScalar(v, rcp_length);
	}

	// -----------------------------------------------------------------------------
	// Vec3

	struct Vec3
	{
		Vec3() = default;
		Vec3(float x, float y, float z)
			: x(x), y(y), z(z) {}
		Vec3(float scalar)
			: x(scalar), y(scalar), z(scalar) {}

		union
		{
			float xyz[3] = { 0 };
			Vec2 xy;
			struct
			{
				float x, y, z;
			};
		};
	};

	static inline Vec3 Vec3Add(const Vec3& v1, const Vec3& v2)
	{
		return Vec3(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z);
	}

	static inline Vec3 Vec3Sub(const Vec3& v1, const Vec3& v2)
	{
		return Vec3(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z);
	}

	static inline Vec3 Vec3MulScalar(const Vec3& v, float s)
	{
		return Vec3(v.x * s, v.y * s, v.z * s);
	}

	static inline float Vec3Dot(const Vec3& v1, const Vec3& v2)
	{
		return v1.x * v2.x + v1.y * v2.y + v1.z * v2.z;
	}

	static inline Vec3 Vec3Cross(const Vec3& v1, const Vec3& v2)
	{
		Vec3 result;
		result.x = v1.y * v2.z - v1.z * v2.y;
		result.y = v1.z * v2.x - v1.x * v2.z;
		result.z = v1.x * v2.y - v1.y * v2.x;
		return result;
	}

	static inline float Vec3Length(const Vec3& v)
	{
		return Vec3Dot(v, v);
	}

	static inline Vec3 Vec3Normalize(const Vec3& v)
	{
		float length = Vec3Length(v);
		float rcp_length = 1.0 / length;
		return Vec3MulScalar(v, rcp_length);
	}

	// -----------------------------------------------------------------------------
	// Vec4

	struct Vec4
	{
		Vec4() = default;
		Vec4(float x, float y, float z, float w)
			: x(x), y(y), z(z), w(w) {}
		Vec4(float scalar)
			: x(scalar), y(scalar), z(scalar), w(scalar) {}

		union
		{
			float xyzw[4] = { 0 };
			Vec2 xy;
			Vec3 xyz;
			struct
			{
				float x, y, z, w;
			};
		};
	};

	static inline Vec4 Vec4Add(const Vec4& v1, const Vec4& v2)
	{
		return Vec4(v1.x + v2.x, v1.y + v2.y, v1.z + v2.z, v1.w + v2.w);
	}

	static inline Vec4 Vec4Sub(const Vec4& v1, const Vec4& v2)
	{
		return Vec4(v1.x - v2.x, v1.y - v2.y, v1.z - v2.z, v1.w - v2.w);
	}

	static inline Vec4 Vec4MulScalar(const Vec4& v, float s)
	{
		return Vec4(v.x * s, v.y * s, v.z * s, v.w * s);
	}

}
