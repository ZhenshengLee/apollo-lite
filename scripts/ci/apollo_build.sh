#! /usr/bin/env bash

###############################################################################
# Copyright 2020 The Apollo Authors. All Rights Reserved.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
###############################################################################

set -e

TOP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd -P)"
source "${TOP_DIR}/scripts/apollo_base.sh"

ARCH="$(uname -m)"

: ${USE_ESD_CAN:=false}
USE_GPU=-1

CMDLINE_OPTIONS=
SHORTHAND_TARGETS=
DISABLED_TARGETS=
CUSTOM_JOBS=${CUSTOM_JOBS:-""}
CUSTOM_RAMS=${CUSTOM_RAMS:-""}
CUSTOM_CPUS=${CUSTOM_CPUS:-""}

declare -A MODULE_DEFINES=(
  # [dreamview]="no_files=true"
)

function _determine_drivers_disabled() {
  if ! ${USE_ESD_CAN}; then
    warning "ESD CAN library supplied by ESD Electronics doesn't exist."
    warning "If you need ESD CAN, please refer to:"
    warning "  third_party/can_card_library/esd_can/README.md"
    DISABLED_TARGETS="${DISABLED_TARGETS} except //modules/drivers/canbus/can_client/esd/..."
  fi
}

function _determine_perception_disabled() {
  if [ "${USE_GPU}" -eq 0 ]; then
    DISABLED_TARGETS="${DISABLED_TARGETS} except //modules/perception/..."
  fi
}

function _determine_localization_disabled() {
  if [ "${ARCH}" != "x86_64" ]; then
    # Skip msf for non-x86_64 platforms
    DISABLED_TARGETS="${disabled} except //modules/localization/msf/..."
  fi
}

function _determine_planning_disabled() {
  if [ "${USE_GPU}" -eq 0 ]; then
    DISABLED_TARGETS="${DISABLED_TARGETS} \
        except //modules/planning/open_space/trajectory_smoother:planning_block"
  fi
}

function _determine_map_disabled() {
  if [ "${USE_GPU}" -eq 0 ]; then
    DISABLED_TARGETS="${DISABLED_TARGETS} except //modules/map/pnc_map:cuda_pnc_util \
                      except //modules/map/pnc_map:cuda_util_test"
  fi
}

function determine_disabled_targets() {
  if [ "$#" -eq 0 ]; then
    _determine_drivers_disabled
    _determine_localization_disabled
    _determine_perception_disabled
    _determine_planning_disabled
    _determine_map_disabled
    echo "${DISABLED_TARGETS}"
    return
  fi

  for component in $@; do
    case "${component}" in
      drivers)
        _determine_drivers_disabled
        ;;
      localization)
        _determine_localization_disabled
        ;;
      perception)
        _determine_perception_disabled
        ;;
      planning)
        _determine_planning_disabled
        ;;
      map)
        _determine_map_disabled
        ;;
    esac
  done

  echo "${DISABLED_TARGETS}"
  # DISABLED_CYBER_MODULES="except //cyber/record:record_file_integration_test"
}

# components="$(echo -e "${@// /\\n}" | sort -u)"
# if [ ${PIPESTATUS[0]} -ne 0 ]; then ... ; fi

function determine_build_targets_and_defines() {
  local targets_all=""
  local defines_all=""

  if [[ "$#" -eq 0 ]]; then
    targets_all="//modules/... union //cyber/..."
  else
    for component in "$@"; do
      if [[ "$component" == "cyber" ]]; then
        if [[ "${HOST_OS}" == "Linux" ]]; then
          targets_all+=" //cyber/... //modules/tools/visualizer/..."
        else
          targets_all+=" //cyber/..."
        fi
      elif [[ -d "${APOLLO_ROOT_DIR}/modules/${component}" ]]; then
        targets_all+=" //modules/${component}/..."
      else
        error "Directory ${APOLLO_ROOT_DIR}/modules/${component} not found. Exiting ..."
        exit 1
      fi

      if [[ -n "${MODULE_DEFINES[$component]}" ]]; then
        defines_all+=" --define ${MODULE_DEFINES[$component]}"
      fi
    done
  fi
  echo "${targets_all} ${defines_all}"
}

function _chk_n_set_gpu_arg() {
  local arg="$1"
  local use_gpu=-1
  if [ "${arg}" = "cpu" ]; then
    use_gpu=0
  elif [ "${arg}" = "gpu" ]; then
    use_gpu=1
  else
    # Do nothing
    return 0
  fi

  if [[ "${USE_GPU}" -lt 0 || "${USE_GPU}" = "${use_gpu}" ]]; then
    USE_GPU="${use_gpu}"
    return 0
  fi

  error "Mixed use of '--config=cpu' and '--config=gpu' may" \
    "lead to unexpected behavior. Exiting..."
  exit 1
}

function parse_cmdline_arguments() {
  local known_options=""
  local remained_args=""

  for ((pos = 1; pos <= $#; pos++)); do #do echo "$#" "$i" "${!i}"; done
    local opt="${!pos}"
    local optarg

    case "${opt}" in
      --config=*)
        optarg="${opt#*=}"
        known_options="${known_options} ${opt}"
        _chk_n_set_gpu_arg "${optarg}"
        ;;
      --config)
        ((++pos))
        optarg="${!pos}"
        known_options="${known_options} ${opt} ${optarg}"
        _chk_n_set_gpu_arg "${optarg}"
        ;;
      --jobs|--jobs=*)
        set -x
        optarg="${opt#*=}"
        if [[ "${optarg}" == "" || "${optarg}" == "${opt}" ]]; then
          ((++pos))
          optarg="${!pos}"
        fi
        CUSTOM_JOBS="${optarg}"
        set +x
        ;;
      --cpus|--cpus=*)
        optarg="${opt#*=}"
        if [[ "${optarg}" == "" || "${optarg}" == "${opt}" ]]; then
          ((++pos))
          optarg="${!pos}"
        fi
        CUSTOM_CPUS="${optarg}"
        ;;
      --rams|--rams=*)
        optarg="${opt#*=}"
        if [[ "${optarg}" == "" || "${optarg}" == "${opt}" ]]; then
          ((++pos))
          optarg="${!pos}"
        fi
        CUSTOM_RAMS="${optarg}"
        ;;
      -c)
        ((++pos))
        optarg="${!pos}"
        known_options="${known_options} ${opt} ${optarg}"
        ;;
      *)
        remained_args="${remained_args} ${opt}"
        ;;
    esac
  done
  # Strip leading whitespaces
  known_options="$(echo "${known_options}" | sed -e 's/^[[:space:]]*//')"
  remained_args="$(echo "${remained_args}" | sed -e 's/^[[:space:]]*//')"

  CMDLINE_OPTIONS="${known_options}"
  SHORTHAND_TARGETS="${remained_args}"
}

function determine_cpu_or_gpu_build() {
  if [ "${USE_GPU}" -lt 0 ]; then
    if [ "${USE_GPU_TARGET}" -eq 0 ]; then
      CMDLINE_OPTIONS="--config=cpu ${CMDLINE_OPTIONS}"
    else
      CMDLINE_OPTIONS="--config=gpu ${CMDLINE_OPTIONS}"
    fi
    # USE_GPU unset, defaults to USE_GPU_TARGET
    USE_GPU="${USE_GPU_TARGET}"
  elif [ "${USE_GPU}" -gt "${USE_GPU_TARGET}" ]; then
    warning "USE_GPU=${USE_GPU} without GPU can't compile. Exiting ..."
    exit 1
  fi

  if [ "${USE_GPU}" -eq 1 ]; then
    ok "Running GPU build on ${ARCH} platform."
  else
    ok "Running CPU build on ${ARCH} platform."
  fi
}

function format_bazel_targets() {
  local targets="$(echo $@ | xargs)"
  targets="${targets// union / }"   # replace all matches of "A union B" to "A B"
  targets="${targets// except / -}" # replaces all matches of "A except B" to "A-B"
  echo "${targets}"
}

function run_bazel_build() {
  if ${USE_ESD_CAN}; then
    CMDLINE_OPTIONS="${CMDLINE_OPTIONS} --define USE_ESD_CAN=${USE_ESD_CAN}"
  fi

  CMDLINE_OPTIONS="$(echo ${CMDLINE_OPTIONS} | xargs)"

  local build_targets
  build_targets="$(determine_build_targets_and_defines ${SHORTHAND_TARGETS})"

  local disabled_targets
  disabled_targets="$(determine_disabled_targets ${SHORTHAND_TARGETS})"
  disabled_targets="$(echo ${disabled_targets} | xargs)"

  # Note(storypku): Workaround for in case "/usr/bin/bazel: Argument list too long"
  # bazel build ${CMDLINE_OPTIONS} ${job_args} $(bazel query ${build_targets})
  local formatted_targets="$(format_bazel_targets ${build_targets} ${disabled_targets})"

  info "Build Overview: "
  info "${TAB}USE_GPU: ${USE_GPU}  [ 0 for CPU, 1 for GPU ]"
  info "${TAB}Bazel Options: ${GREEN}${CMDLINE_OPTIONS}${NO_COLOR}"
  info "${TAB}Build Targets: ${GREEN}${build_targets}${NO_COLOR}"
  info "${TAB}Disabled:      ${YELLOW}${disabled_targets}${NO_COLOR}"

  # default set jobs number according to the total memory size, 2GB per job
  local jobs_args="--jobs=$(awk '/MemTotal/ {printf "%.f", $2/1024/1024/2}' /proc/meminfo)"
  if [[ -n "${CUSTOM_JOBS}" ]]; then
    jobs_args="--jobs=${CUSTOM_JOBS}"
  fi
  # default set cpus number according to the total cpu cores
  local cpu_count=$(($(nproc) / 2))
  local cpus_args="--local_resources=cpu=${cpu_count}"
  if [[ -n "${CUSTOM_CPUS}" ]]; then
    cpus_args="--local_resources=cpu=${CUSTOM_CPUS}"
  fi
  # default set memory size according to the total memory size, 70% of total memory
  local rams_args="--local_resources=memory=HOST_RAM*.7"
  if [[ -n "${CUSTOM_RAMS}" ]]; then
    rams_args="--local_resources=memory=${CUSTOM_RAMS}"
  fi

  local copts_args=""
  if [[ "$(uname -m)" == "aarch64" ]]; then
    local copts_args="--copt=-march=native --host_copt=-march=native --copt=-fPIC --host_copt=-fPIC"
  else
    # x64
    local copts_args="--copt=-mavx2 --host_copt=-mavx2"
  fi
  build_args="${copts_args} ${jobs_args} ${cpus_args} ${rams_args}"
  info "${TAB}Build Command: bazel build ${CMDLINE_OPTIONS} ${build_args} -- ${formatted_targets}"
  bazel build ${CMDLINE_OPTIONS} ${build_args} -- ${formatted_targets}
}

function main() {
  if ! "${APOLLO_IN_DOCKER}"; then
    error "The build operation must be run from within docker container"
    exit 1
  fi
  parse_cmdline_arguments "$@"
  determine_cpu_or_gpu_build

  run_bazel_build

  if [ -z "${SHORTHAND_TARGETS}" ]; then
    SHORTHAND_TARGETS="apollo"
  fi

  success "Done building ${SHORTHAND_TARGETS}. Enjoy!"
}

main "$@"
