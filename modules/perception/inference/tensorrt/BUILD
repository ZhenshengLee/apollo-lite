load("@rules_cc//cc:defs.bzl", "cc_library", "cc_test")
load("//tools:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

cc_library(
    name = "rt_common",
    srcs = ["rt_common.cc"],
    hdrs = [
        "rt_common.h",
    ],
    deps = [
        ":rt_common_without_protobuf",
        "//cyber",
        "//modules/perception/base:common",
        "//modules/perception/proto:rt_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@local_config_cuda//cuda:cudnn_header",
    ],
)

cc_library(
    name = "rt_common_without_protobuf",
    srcs = [],
    hdrs = [
        "rt_common_without_protobuf.h",
        "rt_legacy.h",
    ],
    deps = [
        "//cyber",
        "//modules/perception/base:common",
        "@local_config_cuda//cuda:cudnn_header",
    ],
)

cc_test(
    name = "rt_common_test",
    size = "small",
    srcs = ["rt_common_test.cc"],
    deps = [
        ":rt_common",
        "@com_google_googletest//:gtest_main",
    ],
)

cc_library(
    name = "rt_utils",
    srcs = ["rt_utils.cc"],
    hdrs = ["rt_utils.h"],
    deps = [
        ":rt_common",
        "//cyber",
        "//modules/perception/proto:rt_cc_proto",
        "@local_config_tensorrt//:tensorrt",
    ],
)

cc_test(
    name = "rt_utils_test",
    size = "small",
    srcs = ["rt_utils_test.cc"],
    deps = [
        ":rt_utils",
        "@com_google_googletest//:gtest_main",
    ],
    linkstatic = True,
)

cc_library(
    name = "rt_net",
    srcs = ["rt_net.cc"],
    hdrs = [
        "entropy_calibrator.h",
        "rt_net.h",
    ],
    deps = [
        ":batch_stream",
        ":entropy_calibrator",
        ":rt_common",
        ":rt_utils",
        "//cyber",
        "//modules/perception/base",
        "//modules/perception/inference:inference_lib",
        "//modules/perception/inference/tensorrt/plugins:perception_inference_tensorrt_plugins",
        "@com_google_protobuf//:protobuf",
        "@local_config_tensorrt//:tensorrt",
        "@opencv//:imgcodecs",
    ],
)

cc_test(
    name = "rt_net_test",
    size = "small",
    srcs = ["rt_net_test.cc"],
    data = [
        "//modules/perception/inference:inference_test_data",
    ],
    tags = ["exclude"],
    deps = [
        ":rt_net",
        "//modules/perception/inference/utils:inference_util_cuda_lib",
        "@atlas//:cblas",
        "@com_google_googletest//:gtest_main",
    ],
    linkstatic = True,
)

cc_library(
    name = "entropy_calibrator",
    srcs = ["entropy_calibrator.cc"],
    hdrs = ["entropy_calibrator.h"],
    deps = [
        ":batch_stream",
        ":rt_utils",
        ":rt_common",
        "@com_google_protobuf//:protobuf",
        "@local_config_cuda//cuda:cudart",
        "@local_config_tensorrt//:tensorrt",
        "@opencv//:imgcodecs",
    ],
)

cc_test(
    name = "entropy_calibrator_test",
    size = "small",
    srcs = ["entropy_calibrator_test.cc"],
    copts = ["-fno-access-control"],
    data = [
        "//modules/perception/inference:inference_test_data",
    ],
    deps = [
        ":entropy_calibrator",
        "@com_google_googletest//:gtest_main",
    ],
    linkstatic = True,
)

cc_library(
    name = "batch_stream",
    srcs = ["batch_stream.cc"],
    hdrs = ["batch_stream.h"],
    deps = [
        ":rt_common",
        "//cyber",
        "//modules/perception/proto:rt_cc_proto",
        "@com_google_absl//absl/strings",
        "@com_google_protobuf//:protobuf",
        "@opencv//:imgcodecs",
    ],
)

cc_test(
    name = "batch_stream_test",
    size = "small",
    srcs = ["batch_stream_test.cc"],
    copts = ["-fno-access-control"],
    data = [
        "//modules/perception/inference:inference_test_data",
    ],
    deps = [
        ":batch_stream",
        "@com_google_googletest//:gtest_main",
    ],
    linkstatic = True,
)

cpplint()
