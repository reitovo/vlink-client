#include "i_d3d_src.h"

#include <utility>
#include "d3d_to_frame.h"

IDxToFrameSrc::IDxToFrameSrc(std::shared_ptr<DxToFrame> d3d)
{
    this->d3d = std::move(d3d);
}
