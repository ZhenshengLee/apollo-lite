#! /usr/bin/env bash
TOP_DIR="$(cd "$( dirname "${BASH_SOURCE[0]}" )/.." && pwd -P)"
source ${TOP_DIR}/scripts/apollo.bashrc

export APOLLO_BAZEL_DIST_DIR="${APOLLO_CACHE_DIR}/distdir"
export CYBER_PATH="${APOLLO_ROOT_DIR}/cyber"

bazel_bin_path="${APOLLO_ROOT_DIR}/bazel-bin"
mainboard_path="${bazel_bin_path}/cyber/mainboard"
cyber_tool_path="${bazel_bin_path}/cyber/tools"
recorder_path="${cyber_tool_path}/cyber_recorder"
launch_path="${cyber_tool_path}/cyber_launch"
channel_path="${cyber_tool_path}/cyber_channel"
node_path="${cyber_tool_path}/cyber_node"
service_path="${cyber_tool_path}/cyber_service"
monitor_path="${cyber_tool_path}/cyber_monitor"
visualizer_path="${bazel_bin_path}/modules/tools/visualizer"

# TODO(all): place all these in one place and pathprepend
for entry in "${mainboard_path}" \
    "${recorder_path}" "${monitor_path}"  \
    "${channel_path}" "${node_path}" \
    "${service_path}" \
    "${launch_path}" \
    "${visualizer_path}" ; do
    pathprepend "${entry}"
done
pathprepend "${HOME}/.local/bin" PATH

# pathprepend "${APOLLO_ROOT_DIR}" PYTHONPATH

# Set up the Python environment
# For cyber python
pathprepend "${bazel_bin_path}" PYTHONPATH
# For cyber c++ wrap so
pathprepend "${bazel_bin_path}/cyber/python/internal" PYTHONPATH
# For common_msgs
pathprepend "${bazel_bin_path}/modules" PYTHONPATH

# for protobuf python
export PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python

export CYBER_DOMAIN_ID=80
export CYBER_IP=127.0.0.1

export GLOG_log_dir="${APOLLO_ROOT_DIR}/data/log"
export GLOG_alsologtostderr=0
export GLOG_colorlogtostderr=1
export GLOG_minloglevel=0

export sysmo_start=0

# ncurses need TERMINFO, fix "Error opening terminal: xterm."
export TERMINFO=/lib/terminfo

# for DEBUG log
#export GLOG_v=4

source ${CYBER_PATH}/tools/cyber_tools_auto_complete.bash

# change it if you want to use another language
export LANG=C.UTF-8
