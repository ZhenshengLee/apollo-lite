## Auto generated by `proto_build_generator.sh`
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

cc_proto_library(
    name = "transform_cc_proto",
    deps = [
        ":transform_proto",
    ],
)

proto_library(
    name = "transform_proto",
    srcs = ["transform.proto"],
    deps = [
        "//modules/common_msgs/basic_msgs:geometry_proto",
        "//modules/common_msgs/basic_msgs:header_proto",
    ],
)

py_proto_library(
    name = "transform_py_pb2",
    deps = [":transform_proto"],
)
