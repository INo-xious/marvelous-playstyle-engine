PYTHON ?= python3
VENV_PYTHON := .venv/bin/python
VENV_PIP := .venv/bin/pip

.PHONY: setup engine gui test data report clean

setup:
	$(PYTHON) -m venv .venv
	$(VENV_PIP) install -e '.[dev]'

engine:
	$(MAKE) -C engine all

gui: engine
	$(VENV_PYTHON) -m marvelous_style.gui

test: engine
	$(VENV_PYTHON) -m pytest -q
	$(MAKE) -C engine test

data:
	.venv/bin/marvelous-style all --username MarveIous --root .

report:
	.venv/bin/marvelous-style report --username MarveIous
	.venv/bin/marvelous-style evaluate

clean:
	$(MAKE) -C engine clean
	rm -rf .pytest_cache src/*.egg-info src/*/__pycache__ tests/__pycache__
