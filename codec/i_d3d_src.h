#ifndef I_D3D_SRC_H
#define I_D3D_SRC_H

#include <memory>

struct ID3D11Device;
struct ID3D11DeviceContext;
struct ID3D11Texture2D;
class DxToFrame;

// This interface means providing texture to DxToNdi combiner.
class IDxSrc
{
protected:
    std::shared_ptr<DxToFrame> d3d;
public:
    IDxSrc(std::shared_ptr<DxToFrame> d3d);

    virtual bool copyTo(ID3D11Device* dev, ID3D11DeviceContext* ctx, ID3D11Texture2D *dest) = 0;
};

#endif // I_D3D_SRC_H
