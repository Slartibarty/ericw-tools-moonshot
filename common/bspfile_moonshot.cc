/*  Copyright (C) 2023 Josh Dowell

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

    See file, 'COPYING', for details.
*/

#include <common/bspfile.hh>
#include <common/cmdlib.hh>
#include <common/numeric_cast.hh>

// moonshot_dface_t

moonshot_dface_t::moonshot_dface_t(const mface_t &model)
    : planenum(numeric_cast<int32_t>(model.planenum, "dface_t::planenum")),
      texinfo(model.texinfo),
      firstedge(model.firstedge),
      numedges(model.numedges),
      styles(model.styles),
      lightofs(model.lightofs),
      flags(model.flags)
{
}

moonshot_dface_t::operator mface_t() const
{
    return {
        planenum,
        0,
        firstedge,
        numedges,
        texinfo,
        styles,
        lightofs,
        flags
    };
}

void moonshot_dface_t::stream_write(std::ostream &s) const
{
    s <= std::tie(planenum, texinfo, firstedge, numedges, styles, lightofs, flags);
}

void moonshot_dface_t::stream_read(std::istream &s)
{
    s >= std::tie(planenum, texinfo, firstedge, numedges, styles, lightofs, flags);
}
