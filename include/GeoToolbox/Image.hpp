// Copyright 2024 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <string>
#include <vector>

#include "GeometryTools.hpp"

namespace GeoToolbox
{
	using Color = unsigned;

	constexpr Color White = 0xffffff;
	constexpr Color Black = 0;

	class Image
	{
		int width_ = 0;
		int height_ = 0;
		std::vector<Color> data_;

	public:

		Image(int width, int height)
			: width_{ width }
			, height_{ height }
			, data_(size_t(width) * height, White)
		{
		}

		[[nodiscard]] int GetWidth() const noexcept
		{
			return width_;
		}

		[[nodiscard]] int GetHeight() const noexcept
		{
			return height_;
		}

		[[nodiscard]] std::vector<Color> const& GetData() const noexcept
		{
			return data_;
		}

		void Encode(std::string const& filename) const;

		void Fill(Color);

		void Draw(Vector2 const& location, unsigned color);

		void DrawHorizontal(double y, double left, double right, unsigned color);

		void DrawVertical(double x, double top, double bottom, unsigned color);

		void Draw(Box2 const&, unsigned color);
	};
}
