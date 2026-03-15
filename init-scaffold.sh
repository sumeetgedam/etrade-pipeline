#!/bin/bash
set -euo pipefail

# run inside container, the workspace you created with below command
# docker run -it --name etrade-dev -v "F:\Github\etrade-pipeline:/worksapce" -w /workspace ubuntu:22.04 bash
# current one has small issue as container path in -v is misspelled, 
# so when you login it will got o workspace but we need to move to worksapce for all the files
# so run the below in that directory to make sure your files reflect on windows too


# Create folders
mkdir -p cpp/src cpp/include cpp/tests python/app python/notebooks docs infra data scripts


# README
cat > README.md <<'EOF'
# etrade-pipeline

Monorepo for a toy end to end electronic trading pipeline:
- cpp/ : low-latency components ( feed handler, order book, gateway )
- python/ : analytics, FastAPI, notebooks
- docs/ : architecture and milestones
- infra/ : docker-compose and infra artifacts
- data/ : sample feeds and snapshots

This is an initial scaffold. Follow the milestones in docs/milestones.md
EOF


# .gitignore
cat > .gitignore <<'EOF'
# Build artifacts
/build/
/bin/
/out/
*.o
*.exe

# Python
__pycache__/
*.pyc
.venv/
env/
venv/

# VS code
.vscode/

# Docker
**/node_modules/

# OS
.DS_Store
Thumbs.db
EOF

# Minimal C++ placeholder
cat > cpp/README.md <<'EOF'
C++ folder skeleton. Add CMakeLists.txt and sources here when ready.
EOF

# Minimal python requirements
cat > python/requirements.txt <<'EOF'
fastapi
uvicorn[standard]
pandas
EOF

# Dev set-up helper
cat > scripts/dev-setup.sh <<'EOF'
#!/bin/bash
set -e

# Run inside container to create python venv and install requirements

python3 -m venv .venv
. .venv/bin/activate
pip install --upgrade pip
pip install -r python/requirements.txt

echo "Python venv created at .venv and dependecies Installed."
EOF

chmod +x scripts/dev-setup.sh

# Initialize git repo ( if not already )
if [ ! -d .get ]; then
    git init
fi

# Set local git  name email if environement variables provided
if [ -n "${GIT_NAME:-}" ]; then
    git config user.name "$GIT_NAME"
fi

if [ -n "${GIT_EMAIL:-}" ]; then
    git config user.email "$GIT_EMAIL"
fi


# First commit
git add .
git commit -m "Initial scaffold for etrade-pipeline" || echo "Nothing to commit"


# show layout
echo "Repo layout ( top level ):"
ls -l
echo "Scaffold created and initial commit made."