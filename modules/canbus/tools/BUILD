load("@rules_cc//cc:defs.bzl", "cc_binary")
load("//tools:cpplint.bzl", "cpplint")

package(default_visibility = ["//visibility:public"])

cc_binary(
    name = "canbus_tester",
    srcs = ["canbus_tester.cc"],
    deps = [
        "//cyber",
        "//modules/canbus:canbus_component_lib",
        "//modules/canbus/common:canbus_common",
        "//modules/common/adapters:adapter_gflags",
        "@com_github_gflags_gflags//:gflags",
    ],
)

cc_binary(
    name = "teleop",
    srcs = ["teleop.cc"],
    deps = [
        "//cyber",
        "//modules/canbus/common:canbus_common",
        "//modules/common_msgs/chassis_msgs:chassis_detail_cc_proto",
        "//modules/canbus/vehicle:vehicle_controller_base",
        "//modules/common/adapters:adapter_gflags",
        "//modules/common/configs:vehicle_config_helper",
        "//modules/common/util:message_util",
        "//modules/common_msgs/control_msgs:control_cmd_cc_proto",
        "@com_github_gflags_gflags//:gflags",
    ],
)

cpplint()
