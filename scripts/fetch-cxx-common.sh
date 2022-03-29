#!/usr/bin/env bash
set -eE
set -uo pipefail
DIR="$( cd "$( dirname "${BASH_SOURCE[0]}" )" >/dev/null 2>&1 && pwd )"
ROOT_DIR="$(realpath ${DIR}/..)"
LOG_FILE=${ROOT_DIR}/cxx-common.log
PLATFORM=$(uname -m)
LLVM=${LLVM:-13}

function log_msg {
    echo "${1}"
    echo "${1}" >> "${LOG_FILE}"
}

function setup_env {
  if [[ "${PLATFORM}" == "x86_64" ]]
  then
    export ARCH=amd64
  elif [[ "${PLATFORM}" == "arm64" || "${PLATFORM}" == "aarch64" ]]
  then
    export ARCH=arm64
  else
    log_msg "Unsupported platform: ${PLATFORM}!"
    exit 1
  fi

  if [[ "${OSTYPE}" == "darwin"* ]]
  then
    XCODE="_xcode-13.0";
    OS="macos-11"
  elif [[ ${OSTYPE} == "linux"* ]]
  then
    XCODE=""
    OS="ubuntu-20.04"
  else
    log_msg "Unsupported OS: ${OSTYPE}"
    exit 1
  fi

  export CXX_COMMON_NAME=vcpkg_${OS}_llvm-${LLVM}${XCODE}_${ARCH}

  log_msg "Arch: ${ARCH}"
  log_msg "CXX_COMMON_NAME: ${CXX_COMMON_NAME}"
}

function install_cxx_common {
  local base_dir=${1}
  if [[ ! -f "${base_dir}/cxx-common.tar.xz" ]]
  then
    log_msg "Fetching cxx-common from Github"
    URL="https://github.com/lifting-bits/cxx-common/releases/latest/download/${CXX_COMMON_NAME}.tar.xz"
    GITHUB_LIBS="cxx-common.tar.xz"
    pushd "${base_dir}" >/dev/null
    log_msg "Fetching: ${URL} to [$(pwd)/${GITHUB_LIBS}]"
    curl -sS -o "${GITHUB_LIBS}" -L "${URL}" \
      >> ${LOG_FILE} 2>&1
    tar -xJf "${GITHUB_LIBS}"
    # do not archive cxx-common zip for export mode
    popd >/dev/null
  fi

  export VCPKG_ROOT="${base_dir}/${CXX_COMMON_NAME}"
}

setup_env
mkdir -p "${ROOT_DIR}/../downloads"
install_cxx_common "${ROOT_DIR}/../downloads"
