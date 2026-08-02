// A forwarding IDXGISwapChain wrapper, for the H5 experiment.
//
// This is what Streamline's sl.interposer, ReShade and similar overlays do:
// hand the application their own COM object implementing the interface, and
// forward to the real swapchain underneath. It matters because our hooking
// strategy patches the vtable of a *real* DXGI swapchain, and a proxy has its
// own vtable that we never touch.
//
// Every method forwards. The interesting one is Present: whether patching the
// real vtable still catches a present issued through this wrapper is exactly
// the question 20_OPEN_QUESTIONS §H5 asks.

#ifndef FRAMELEDGER_PROXY_SWAPCHAIN_H
#define FRAMELEDGER_PROXY_SWAPCHAIN_H

#include <windows.h>

#include <dxgi1_2.h>

#include <atomic>

namespace fl::harness {

class ProxySwapChain final : public IDXGISwapChain {
public:
    explicit ProxySwapChain(IDXGISwapChain* real) : real_(real) { real_->AddRef(); }

    // Counts presents that arrived at the PROXY, as distinct from presents the
    // hook on the real vtable observed. Comparing the two is the experiment.
    std::atomic<unsigned> proxyPresentCount{0};

    // --- IUnknown ---------------------------------------------------------
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppv) override {
        if (riid == __uuidof(IDXGISwapChain) || riid == __uuidof(IUnknown)) {
            AddRef();
            *ppv = this;
            return S_OK;
        }
        return real_->QueryInterface(riid, ppv);
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        const ULONG n = --refs_;
        if (n == 0) {
            real_->Release();
            delete this;
        }
        return n;
    }

    // --- IDXGIObject ------------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID g, UINT s, const void* d) override {
        return real_->SetPrivateData(g, s, d);
    }
    HRESULT STDMETHODCALLTYPE SetPrivateDataInterface(REFGUID g, const IUnknown* u) override {
        return real_->SetPrivateDataInterface(g, u);
    }
    HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID g, UINT* s, void* d) override {
        return real_->GetPrivateData(g, s, d);
    }
    HRESULT STDMETHODCALLTYPE GetParent(REFIID riid, void** pp) override { return real_->GetParent(riid, pp); }

    // --- IDXGIDeviceSubObject ---------------------------------------------
    HRESULT STDMETHODCALLTYPE GetDevice(REFIID riid, void** pp) override { return real_->GetDevice(riid, pp); }

    // --- IDXGISwapChain ---------------------------------------------------
    HRESULT STDMETHODCALLTYPE Present(UINT sync, UINT flags) override {
        proxyPresentCount.fetch_add(1, std::memory_order_relaxed);
        // Forwarding through the real object's interface pointer — an ordinary
        // virtual dispatch, so it goes through the real vtable. That is why
        // the result of this experiment is not obvious either way.
        return real_->Present(sync, flags);
    }
    HRESULT STDMETHODCALLTYPE GetBuffer(UINT i, REFIID riid, void** pp) override {
        return real_->GetBuffer(i, riid, pp);
    }
    HRESULT STDMETHODCALLTYPE SetFullscreenState(BOOL fs, IDXGIOutput* o) override {
        return real_->SetFullscreenState(fs, o);
    }
    HRESULT STDMETHODCALLTYPE GetFullscreenState(BOOL* fs, IDXGIOutput** o) override {
        return real_->GetFullscreenState(fs, o);
    }
    HRESULT STDMETHODCALLTYPE GetDesc(DXGI_SWAP_CHAIN_DESC* d) override { return real_->GetDesc(d); }
    HRESULT STDMETHODCALLTYPE ResizeBuffers(UINT c, UINT w, UINT h, DXGI_FORMAT f, UINT fl) override {
        return real_->ResizeBuffers(c, w, h, f, fl);
    }
    HRESULT STDMETHODCALLTYPE ResizeTarget(const DXGI_MODE_DESC* m) override { return real_->ResizeTarget(m); }
    HRESULT STDMETHODCALLTYPE GetContainingOutput(IDXGIOutput** o) override { return real_->GetContainingOutput(o); }
    HRESULT STDMETHODCALLTYPE GetFrameStatistics(DXGI_FRAME_STATISTICS* s) override {
        return real_->GetFrameStatistics(s);
    }
    HRESULT STDMETHODCALLTYPE GetLastPresentCount(UINT* c) override { return real_->GetLastPresentCount(c); }

private:
    ~ProxySwapChain() = default;

    IDXGISwapChain*    real_;
    std::atomic<ULONG> refs_{1};
};

}    // namespace fl::harness

#endif    // FRAMELEDGER_PROXY_SWAPCHAIN_H
