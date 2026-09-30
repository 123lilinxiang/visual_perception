#pragma once
#include "NvInferVersion.h"
#include "cuda_runtime_api.h"
#include <cstdint>
#include <string>
namespace nvinfer1 {
#if NV_TENSORRT_MAJOR >= 10
using DimValue = int64_t;
#else
using DimValue = int32_t;
#endif
struct Dims {int nbDims=0; DimValue d[8]={};};
struct Dims4 : Dims { Dims4(int n,int c,int h,int w){nbDims=4;d[0]=n;d[1]=c;d[2]=h;d[3]=w;} };
enum class DataType{kFLOAT,kHALF};
enum class TensorLocation{kDEVICE};
enum class TensorFormat{kLINEAR};
enum class TensorIOMode{kINPUT,kOUTPUT};
enum class OptProfileSelector{kOPT};
struct ILogger {enum class Severity{kINTERNAL_ERROR,kERROR,kWARNING,kINFO};virtual void log(Severity,const char*) noexcept=0;virtual ~ILogger()=default;};
inline bool dynamic_model=true, bad_type=false, bad_rank=false, enqueue_failure=false;
inline int output_channels=5;
inline int live_objects=0;
struct IExecutionContext {
 Dims shape;void* in=nullptr;void* out=nullptr;
 IExecutionContext(){++live_objects;} ~IExecutionContext(){--live_objects;}
 bool set(const Dims& d){if(d.d[2]>64 || d.d[3]>64 || (!dynamic_model && (d.d[2]!=4 || d.d[3]!=4)))return false;shape=d;return true;}
 Dims output(){ if(!shape.nbDims)throw std::runtime_error("output queried before input shape"); return Dims{3,{1,output_channels,shape.d[2]*shape.d[3]}};}
 bool run(){if(enqueue_failure)return false;size_t n=static_cast<size_t>(output_channels*shape.d[2]*shape.d[3]);if(!in||!out||allocations.at(out)<n*4)throw std::runtime_error("bad bindings");for(size_t i=0;i<n;i++)static_cast<float*>(out)[i]=7;return true;}
#if NV_TENSORRT_MAJOR >= 10
 bool setInputShape(const char* name,const Dims& d){if(std::string(name)!="image")throw std::runtime_error("wrong input name");return set(d);}
 Dims getTensorShape(const char*){return output();}
 bool setTensorAddress(const char* name,void* p){(std::string(name)=="image"?in:out)=p;return true;}
 bool enqueueV3(void*){return run();}
#else
 bool setBindingDimensions(int idx,const Dims& d){if(idx!=1)throw std::runtime_error("wrong input index");return set(d);}
 Dims getBindingDimensions(int idx){if(idx!=0)throw std::runtime_error("wrong output index");return output();}
 bool enqueueV2(void* const* ptrs,void*,void*){out=ptrs[0];in=ptrs[1];if(ptrs[2]||ptrs[3])throw std::runtime_error("profile 1 unexpectedly populated");return run();}
#endif
};
struct ICudaEngine {
 ICudaEngine(){++live_objects;}~ICudaEngine(){--live_objects;}
 IExecutionContext* createExecutionContext(){return new IExecutionContext;}
 Dims input(){return bad_rank?Dims{2,{1,3}}:Dims{4,{1,3,dynamic_model?-1:4,dynamic_model?-1:4}};}
#if NV_TENSORRT_MAJOR >= 10
 int getNbIOTensors(){return 2;}
 const char* getIOTensorName(int i){return i==0?"prediction":"image";}
 TensorIOMode getTensorIOMode(const char* s){return std::string(s)=="image"?TensorIOMode::kINPUT:TensorIOMode::kOUTPUT;}
 DataType getTensorDataType(const char*){return bad_type?DataType::kHALF:DataType::kFLOAT;}
 TensorLocation getTensorLocation(const char*){return TensorLocation::kDEVICE;}
 TensorFormat getTensorFormat(const char*){return TensorFormat::kLINEAR;}
 bool isShapeInferenceIO(const char*){return false;}
 Dims getTensorShape(const char* s){return std::string(s)=="image"?input():Dims{3,{1,5,-1}};}
 Dims getProfileShape(const char*,int,OptProfileSelector){return Dims4{1,3,4,4};}
#else
 bool hasImplicitBatchDimension(){return false;}
 int getNbBindings(){return 4;}
 int getNbOptimizationProfiles(){return 2;}
 const char* getBindingName(int i){return i==0?"prediction":"image";}
 bool bindingIsInput(int i){return i==1;}
 DataType getBindingDataType(int){return bad_type?DataType::kHALF:DataType::kFLOAT;}
 TensorLocation getLocation(int){return TensorLocation::kDEVICE;}
 TensorFormat getBindingFormat(int){return TensorFormat::kLINEAR;}
 bool isShapeBinding(int){return false;}
 Dims getBindingDimensions(int i){return i==1?input():Dims{3,{1,5,-1}};}
 Dims getProfileDimensions(int,int,OptProfileSelector){return Dims4{1,3,4,4};}
#endif
};
struct IRuntime {IRuntime(){++live_objects;}~IRuntime(){--live_objects;} ICudaEngine* deserializeCudaEngine(const void*,size_t){return new ICudaEngine;}};
inline IRuntime* createInferRuntime(ILogger&){return new IRuntime;}
}
