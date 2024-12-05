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

	void Image::Draw(Vector2 const& location, unsigned color)
	{
		data_[std::lround(location[1]) * width_ + std::lround(location[0])] = color;
	}

	void Image::DrawHorizontal(double y, double left, double right, unsigned color)
	{
		auto const start = std::lround(y) * width_ + std::lround(left);
		std::fill_n(data_.begin() + start, std::lround(right) - std::lround(left), color);
	}

	void Image::DrawVertical(double x, double top, double bottom, unsigned color)
	{
		for (auto y = std::lround(top); y <= std::lround(bottom); ++y)
		{
			Draw({ x, double(y) }, color);
		}
	}

	void Image::Draw(Box2 const& box, unsigned color)
	{
		DrawHorizontal(box.Min()[1], box.Min()[0], box.Max()[0], color);
		DrawHorizontal(box.Max()[1], box.Min()[0], box.Max()[0], color);
		DrawVertical(box.Min()[0], box.Min()[1], box.Max()[1], color);
		DrawVertical(box.Max()[0], box.Min()[1], box.Max()[1], color);
	}
}
