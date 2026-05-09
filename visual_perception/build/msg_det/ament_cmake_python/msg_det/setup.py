from setuptools import find_packages
from setuptools import setup

setup(
    name='msg_det',
    version='0.0.1',
    packages=find_packages(
        include=('msg_det', 'msg_det.*')),
)
