#include "i_d3d_src.h"
#include "d3d_to_frame.h"

IDxSrc::IDxSrc(std::shared_ptr<DxToFrame> d3d)
{
    this->d3d = d3d;
}
