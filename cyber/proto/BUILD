## Auto generated by `proto_build_generator.sh`
load("@com_google_protobuf//bazel:proto_library.bzl", "proto_library")
load("@com_google_protobuf//bazel:cc_proto_library.bzl", "cc_proto_library")
load("@com_google_protobuf//bazel:py_proto_library.bzl", "py_proto_library")

package(default_visibility = ["//visibility:public"])

cc_proto_library(
    name = "choreography_conf_cc_proto",
    deps = [
        ":choreography_conf_proto",
    ],
)

proto_library(
    name = "choreography_conf_proto",
    srcs = ["choreography_conf.proto"],
)

py_proto_library(
    name = "choreography_conf_py_pb2",
    deps = [":choreography_conf_proto"],
)

cc_proto_library(
    name = "classic_conf_cc_proto",
    deps = [
        ":classic_conf_proto",
    ],
)

proto_library(
    name = "classic_conf_proto",
    srcs = ["classic_conf.proto"],
)

py_proto_library(
    name = "classic_conf_py_pb2",
    deps = [":classic_conf_proto"],
)

cc_proto_library(
    name = "clock_cc_proto",
    deps = [
        ":clock_proto",
    ],
)

proto_library(
    name = "clock_proto",
    srcs = ["clock.proto"],
)

py_proto_library(
    name = "clock_py_pb2",
    deps = [":clock_proto"],
)

cc_proto_library(
    name = "component_conf_cc_proto",
    deps = [
        ":component_conf_proto",
    ],
)

proto_library(
    name = "component_conf_proto",
    srcs = ["component_conf.proto"],
    deps = [
        ":qos_profile_proto",
    ],
)

py_proto_library(
    name = "component_conf_py_pb2",
    deps = [":component_conf_proto"],
)

cc_proto_library(
    name = "cyber_conf_cc_proto",
    deps = [
        ":cyber_conf_proto",
    ],
)

proto_library(
    name = "cyber_conf_proto",
    srcs = ["cyber_conf.proto"],
    deps = [
        ":perf_conf_proto",
        ":run_mode_conf_proto",
        ":scheduler_conf_proto",
        ":transport_conf_proto",
    ],
)

py_proto_library(
    name = "cyber_conf_py_pb2",
    deps = [":cyber_conf_proto"],
)

cc_proto_library(
    name = "dag_conf_cc_proto",
    deps = [
        ":dag_conf_proto",
    ],
)

proto_library(
    name = "dag_conf_proto",
    srcs = ["dag_conf.proto"],
    deps = [
        ":component_conf_proto",
    ],
)

py_proto_library(
    name = "dag_conf_py_pb2",
    deps = [":dag_conf_proto"],
)

cc_proto_library(
    name = "parameter_cc_proto",
    deps = [
        ":parameter_proto",
    ],
)

proto_library(
    name = "parameter_proto",
    srcs = ["parameter.proto"],
)

py_proto_library(
    name = "parameter_py_pb2",
    deps = [":parameter_proto"],
)

cc_proto_library(
    name = "perf_conf_cc_proto",
    deps = [
        ":perf_conf_proto",
    ],
)

proto_library(
    name = "perf_conf_proto",
    srcs = ["perf_conf.proto"],
)

py_proto_library(
    name = "perf_conf_py_pb2",
    deps = [":perf_conf_proto"],
)

cc_proto_library(
    name = "proto_desc_cc_proto",
    deps = [
        ":proto_desc_proto",
    ],
)

proto_library(
    name = "proto_desc_proto",
    srcs = ["proto_desc.proto"],
)

py_proto_library(
    name = "proto_desc_py_pb2",
    deps = [":proto_desc_proto"],
)

cc_proto_library(
    name = "qos_profile_cc_proto",
    deps = [
        ":qos_profile_proto",
    ],
)

proto_library(
    name = "qos_profile_proto",
    srcs = ["qos_profile.proto"],
)

py_proto_library(
    name = "qos_profile_py_pb2",
    deps = [":qos_profile_proto"],
)

cc_proto_library(
    name = "role_attributes_cc_proto",
    deps = [
        ":role_attributes_proto",
    ],
)

proto_library(
    name = "role_attributes_proto",
    srcs = ["role_attributes.proto"],
    deps = [
        ":qos_profile_proto",
    ],
)

py_proto_library(
    name = "role_attributes_py_pb2",
    deps = [":role_attributes_proto"],
)

cc_proto_library(
    name = "run_mode_conf_cc_proto",
    deps = [
        ":run_mode_conf_proto",
    ],
)

proto_library(
    name = "run_mode_conf_proto",
    srcs = ["run_mode_conf.proto"],
)

py_proto_library(
    name = "run_mode_conf_py_pb2",
    deps = [":run_mode_conf_proto"],
)

cc_proto_library(
    name = "scheduler_conf_cc_proto",
    deps = [
        ":scheduler_conf_proto",
    ],
)

proto_library(
    name = "scheduler_conf_proto",
    srcs = ["scheduler_conf.proto"],
    deps = [
        ":choreography_conf_proto",
        ":classic_conf_proto",
    ],
)

py_proto_library(
    name = "scheduler_conf_py_pb2",
    deps = [":scheduler_conf_proto"],
)

cc_proto_library(
    name = "topology_change_cc_proto",
    deps = [
        ":topology_change_proto",
    ],
)

proto_library(
    name = "topology_change_proto",
    srcs = ["topology_change.proto"],
    deps = [
        ":role_attributes_proto",
    ],
)

py_proto_library(
    name = "topology_change_py_pb2",
    deps = [":topology_change_proto"],
)

cc_proto_library(
    name = "unit_test_cc_proto",
    deps = [
        ":unit_test_proto",
    ],
)

proto_library(
    name = "unit_test_proto",
    srcs = ["unit_test.proto"],
)

py_proto_library(
    name = "unit_test_py_pb2",
    deps = [":unit_test_proto"],
)

cc_proto_library(
    name = "record_cc_proto",
    deps = [
        ":record_proto",
    ],
)

proto_library(
    name = "record_proto",
    srcs = ["record.proto"],
)

py_proto_library(
    name = "record_py_pb2",
    deps = [":record_proto"],
)

cc_proto_library(
    name = "simple_cc_proto",
    deps = [
        ":simple_proto",
    ],
)

proto_library(
    name = "simple_proto",
    srcs = ["simple.proto"],
    deps = [
        "//modules/common_msgs/basic_msgs:header_proto",
    ],
)

py_proto_library(
    name = "simple_py_pb2",
    deps = [":simple_proto"],
)

cc_proto_library(
    name = "transport_conf_cc_proto",
    deps = [
        ":transport_conf_proto",
    ],
)

proto_library(
    name = "transport_conf_proto",
    srcs = ["transport_conf.proto"],
)

py_proto_library(
    name = "transport_conf_py_pb2",
    deps = [":transport_conf_proto"],
)
