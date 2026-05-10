# ECE2071_TeamB09
porject repository for the ECE2071 project

# PROJECT: Proximity Triggered Data Aquisition System
## Description
Our project intends to capture data from audio jack and refine it to produce a desired outputs

ustilising 2 STM32's:
1. Sampling STM:\
samples data from audio jack and transfers it to processing STM
>ECE2071ProjectV1-sampling-main.c
2. Processing STm:\
reterive samples and cleans them using outlier rejection and moving average filter, then packs 2 samples over 3 bytes to trransfer to computer.
>ECE2071ProjectV11-processing-main.c

3. Python script:\
Finally python script collects cleaned data, unpacks it and provides 3 types of outputs (PNG,WAVE,CSV) files.
>PythonScript.py




## contributors:
- Seb
- Jake Vorrath
- Wenhao Yan
- Chris
