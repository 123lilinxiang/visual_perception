#pragma once
#include <cstdlib>
#include <cstring>
#include <map>
#include <stdexcept>
using cudaStream_t = void*;
using cudaError_t = int;
constexpr int cudaSuccess=0, cudaHostAllocDefault=0;
enum cudaMemcpyKind {cudaMemcpyHostToDevice,cudaMemcpyDeviceToHost};
inline std::map<void*,size_t> allocations;
inline const char* cudaGetErrorString(int){return "mock error";}
inline int cudaStreamCreate(void** p){*p=reinterpret_cast<void*>(1);return 0;}
inline int cudaStreamSynchronize(void*){return 0;}
inline int cudaStreamDestroy(void*){return 0;}
inline int cudaMalloc(void** p,size_t n){*p=malloc(n);allocations[*p]=n;return *p?0:1;}
inline int cudaHostAlloc(void** p,size_t n,unsigned){return cudaMalloc(p,n);}
inline int cudaFree(void* p){allocations.erase(p);free(p);return 0;}
inline int cudaFreeHost(void* p){return cudaFree(p);}
inline int cudaMemcpyAsync(void* dst,const void* src,size_t n,cudaMemcpyKind,void*){
 if(allocations.at(dst)<n || allocations.at(const_cast<void*>(src))<n) throw std::runtime_error("copy overflow");
 memcpy(dst,src,n);return 0;
}
inline int cudaMemsetAsync(void* dst,int c,size_t n,void*){if(allocations.at(dst)<n) throw std::runtime_error("memset overflow");memset(dst,c,n);return 0;}
