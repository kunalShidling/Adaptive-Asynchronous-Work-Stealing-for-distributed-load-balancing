@echo off
setlocal
echo ======================================================================
echo Launching Adaptive Work-Stealing Scheduler Dashboard
echo ======================================================================

cd /d d:\MAP_PROJECT
python -m streamlit run python/app.py
