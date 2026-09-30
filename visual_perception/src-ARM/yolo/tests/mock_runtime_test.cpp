// Unit tests use fake TensorRT/CUDA; these do NOT prove GPU inference.
#include "trt-runtime.hpp"
#include <cassert>
#include <functional>
struct Binding {size_t size=1,dsize=1;nvinfer1::Dims dims{};std::string name;};
struct TestRuntime : yolo_trt::Runtime<Binding> {
 using Runtime::Runtime;
 using Runtime::set_input_shape;
 using Runtime::copy_input;
 using Runtime::validate_output;
};
void fails(const std::function<void()>& f){bool threw=false;try{f();}catch(const std::runtime_error&){threw=true;}assert(threw);}
int main(int argc, char** argv){
 assert(argc == 2);
 const std::string path=argv[1];std::ofstream(path)<<"mock";
 fails([&]{TestRuntime r(path+".missing");});
 for(bool dynamic:{false,true}){
  nvinfer1::dynamic_model=dynamic;
  {TestRuntime r(path);fails([&]{r.infer();});r.make_pipe(true);r.make_pipe();
   r.validate_output(1,4);fails([&]{r.validate_output(2,4);});
   if(dynamic){r.set_input_shape(nvinfer1::Dims4{1,3,8,8});assert(r.output_bindings[0].size==320);}
   size_t n=r.input_bindings[0].size;std::vector<float> data(n,1);
   fails([&]{r.copy_input(data.data(),4);});r.copy_input(data.data(),n*4);r.infer();assert(static_cast<float*>(r.host_ptrs[0])[0]==7);
   fails([&]{r.set_input_shape(nvinfer1::Dims4{1,3,128,128});});
   if(dynamic){r.set_input_shape(nvinfer1::Dims4{1,3,4,4});r.infer();assert(r.output_bindings[0].size==80);}
   else{fails([&]{r.set_input_shape(nvinfer1::Dims4{1,3,8,8});});}
   nvinfer1::enqueue_failure=true;fails([&]{r.infer();});nvinfer1::enqueue_failure=false;
  }
  assert(allocations.empty());assert(nvinfer1::live_objects==0);
 }
 nvinfer1::bad_type=true;fails([&]{TestRuntime r(path);});nvinfer1::bad_type=false;assert(nvinfer1::live_objects==0);
 nvinfer1::bad_rank=true;fails([&]{TestRuntime r(path);});nvinfer1::bad_rank=false;assert(nvinfer1::live_objects==0);
 nvinfer1::output_channels=6;
 {TestRuntime r(path);r.make_pipe(false);r.validate_output(1,5);fails([&]{r.validate_output(1,4);});}
 assert(allocations.empty());assert(nvinfer1::live_objects==0);
 std::cout<<"PASS runtime mapping, profiles, static/dynamic shapes, copies, errors, cleanup TRT "<<NV_TENSORRT_MAJOR<<'\n';
}
