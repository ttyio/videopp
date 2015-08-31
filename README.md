# videopp
video postprocess using CUDA

How to build & run:
> make dbg=1      <br/>      
> ./bin/x86_64/linux/debug/videoPP            // the input is in ./plush1_720p_10s.m2v, output is in ./output.mp4
 
How to implement the image filter
> Edit workload in function cudaLaunchARGBpostprocess(), file cudaProcessFrame.cpp <br/>
> Edit kernel code in function ARGBpostprocess() file videoPP.cu
