# Configuration file for the Sphinx documentation builder.
#
# For the full list of built-in configuration values, see the documentation:
# https://www.sphinx-doc.org/en/master/usage/configuration.html

# -- Project information -----------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#project-information

project = 'GeoMX'
copyright = '2023, Zonghang Li'
author = 'Zonghang Li'
version = release = '1.0'

# -- General configuration ---------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#general-configuration

extensions = [
    'autoapi.extension',
    'sphinx.ext.viewcode',
    'sphinx.ext.githubpages',
    'sphinx.ext.mathjax',
    # 'myst_parser', # style extension
    'sphinx.ext.intersphinx',
    'sphinx.ext.todo',
    'sphinxcontrib.napoleon',
    'sphinx.ext.autosectionlabel',
    'sphinx_design',
    'sphinxcontrib.bibtex',
]

templates_path = ['_templates']
exclude_patterns = []

# -- Options for HTML output -------------------------------------------------
# https://www.sphinx-doc.org/en/master/usage/configuration.html#options-for-html-output
# html_theme = 'sphinx_rtd_theme'
html_theme = 'furo'
# html_favicon = "source/_static/favicon.png"

html_theme_options = {
    "sidebar_hide_name": True,
    # "light_logo": "logo.svg",
    # "dark_logo": "logo.svg",
}

html_static_path = ['source/_static']

# for 'sphinxcontrib.bibtex' extension
# bibtex_bibfiles = ['refs.bib']
# bibtex_default_style = 'unsrt'

# autosectionlabel throws warnings if section names are duplicated.
# The following tells autosectionlabel to not throw a warning for
# duplicated section names that are in different documents.
autosectionlabel_prefix_document = True

# The name of the Pygments (syntax highlighting) style to use.
pygments_style = 'sphinx'

# Config for 'sphinx.ext.todo'
todo_include_todos = True

