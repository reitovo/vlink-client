#include "i_d3d_src.h"
#include "d3d_to_ndi.h"

IDxSrc::IDxSrc(std::shared_ptr<DxToNdi> d3d)
{
    this->d3d = d3d;
}
