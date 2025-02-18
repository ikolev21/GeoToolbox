// Copyright 2024-2025 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#include "GeoToolbox/Image.hpp"

#ifdef ENABLE_LODEPNG
#	include "lodepng/lodepng.h"
#endif

namespace GeoToolbox
{
	void Image::Encode([[maybe_unused]] std::string const& filename) const
	{
#ifdef ENABLE_LODEPNG
		lodepng::encode(filename, reinterpret_cast<std::uint8_t const*>(data_.data()), width_, height_);
#else
		throw std::logic_error("LodePNG not enabled, can't save image");
#endif // ENABLE_LODEPNG
	}

	void Image::Fill(Color color)
	{
		std::fill(data_.begin(), data_.end(), color);
	}

	void Image::Draw(int x, int y, Color color)
	{
		if (x < 0 || x > width_ - 1
			|| y < 0 || y > height_ - 1)
		{
			return;
		}

		data_[y * width_ + x] = color;
	}

	void Image::Draw(Vector2 const& location, Color color)
	{
		auto const x = std::lround(location[0]);
		auto const y = std::lround(FlipY(location[1]));
		Draw(x, y, color);
	}

	void Image::DrawHorizontal(double y, double left, double right, Color color)
	{
		if (right < 0 || left > width_ - 1 || left > right || y < 0 || y > height_ - 1)
		{
			return;
		}

		left = std::max(left, 0.0);
		right = std::min(right, double(width_ - 1));
		y = FlipY(y);
		auto const start = std::lround(y) * width_ + std::lround(left);
		std::fill_n(data_.begin() + start, std::lround(right) - std::lround(left), color);
	}

	void Image::DrawVertical(double x, double bottom, double top, Color color)
	{
		if (top < 0 || bottom > height_ - 1 || bottom > top || x < 0 || x > width_ - 1)
		{
			return;
		}

		bottom = FlipY(std::max(bottom, 0.0));
		top = FlipY(std::min(top, double(height_ - 1)));
		auto const xLong = std::lround(x);
		for (auto y = std::lround(top); y <= std::lround(bottom); ++y)
		{
			Draw(xLong, y, color);
		}
	}

	void Image::Draw(Box2 const& box, Color color)
	{
		DrawHorizontal(box.Min()[1], box.Min()[0], box.Max()[0], color);
		DrawHorizontal(box.Max()[1], box.Min()[0], box.Max()[0], color);
		DrawVertical(box.Min()[0], box.Min()[1], box.Max()[1], color);
		DrawVertical(box.Max()[0], box.Min()[1], box.Max()[1], color);
	}
}
