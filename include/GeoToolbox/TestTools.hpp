// Copyright 2024-2026 Ivan Kolev
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include "GeoToolbox/Image.hpp"
#include "GeoToolbox/SpatialTools.hpp"

namespace GeoToolbox
{
	template <typename TSpatialKey>
	void DrawSpatialKeys(Image& image, std::vector<TSpatialKey> const& keys, Box2 const& boundingBox)
	{
		static constexpr auto MaxElements = 1000000;

		auto const step = keys.size() < MaxElements ? 1.0 : double(keys.size()) / MaxElements;
		image.Fill(White);
		auto const ImageBox = Box2{ Vector2{}, Vector2{ double(image.GetWidth() - 1), double(image.GetHeight() - 1) } };
		for (auto i = 0.0; size_t(i) < keys.size(); i += step)
		{
			if constexpr (SpatialKeyIsPoint<TSpatialKey>)
			{
				auto const point = keys[size_t(i)];
				auto const pos = ReInterpolate(point, boundingBox, ImageBox);
				image.Draw(pos, Black);
			}
			else
			{
				static_assert(SpatialKeyIsBox<TSpatialKey>);
				auto const minPoint = keys[size_t(i)].Min();
				auto const maxPoint = keys[size_t(i)].Max();
				auto const min = ReInterpolate(minPoint, boundingBox, ImageBox);
				auto const max = ReInterpolate(maxPoint, boundingBox, ImageBox);
				image.Draw(Box2{ min, max }, Black);
			}
		}
	}
}
