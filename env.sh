# UNA Watch development environment. Usage: source env.sh
export UNA_SDK="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)/una-sdk"
export PATH="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" && pwd)/.venv/bin:$PATH"
