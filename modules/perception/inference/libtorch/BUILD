load("//tools:cpplint.bzl", "cpplint")
load("@rules_cc//cc:defs.bzl", "cc_library")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "torch_net",
    srcs = ["torch_net.cc"],
    hdrs = ["torch_net.h"],
    deps = [
        "//modules/perception/inference:inference_lib",
        "@libtorch_gpu",
        "@com_google_protobuf//:protobuf",
        "@opencv//:imgcodecs",
        "@local_config_cuda//cuda:cudart",
    ],
)

cpplint()
