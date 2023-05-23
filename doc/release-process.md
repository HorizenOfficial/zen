RELEASE PROCESS
---------------------------
​
This guide illustrates all the steps needed for producing a new version of `zend`.
​
Requirements
------------
- Python script `contrib/devtools/release_management/release_preparation.py`
- yaml configuration template file `contrib/devtools/release_management/release_preparation_config.yaml` (or a previously used yaml file)
- `zend` installation with synchronized mainnet and testnet + debug.log for both networks
- version release and deprecation info:
  - version release height (implicitly version release date)
  - version lifetime (until deprecation) in weeks
- updated zend repository, with main branch checked out (it is not required to be at main branch tip)

Preparation: configuring release preparation script
------------------------------------------------
- make sure the yaml configuration file for `release_preparation.py` script is correct:
  - "checkpoint_height" must be set according to the following rules:
    - at least 100 confirmations
    - timestamps of 3 preceding and following blocks are all in series
    - no forked blocks in 3 preceding and following blocks (check via getchaintips RPC command on running node)
    - round the number up to hundreds
  - "total_transactions" must be set looking at the corresponding checkpoint `UpdateTip` logline in debug.log file
  - "release_date", "approx_release_height" and "weeks_until_deprecation" must be set as per deprecation spreadsheet
  - if running the script under standard flow, set "skip" and "stop" values to "False" for all the script steps
- additional info are contained in the comments of the configuration file
- check for `contrib/devtools/README.md` for further details
- put the yaml configuration file in folder `contrib/devtools/release_management/configs` naming it as `release_preparation_config-X.Y.Z.yaml`

​Execution: running release preparation script
------------------------------------------------
- open a terminal located at repository root
- run `release_preparation.py` providing yaml configuration as the only one command line parameter
- wait for script completion
- check for script output, making sure no error has been raised

​Finalization: release officialization
------------------------------------------------
- push release preparation local branch to remote branch
- create associated release branch on remote (note this release branch is different from release preparation branch)
- open a PR merging release preparation into release branch and wait for approval
- once the release preparation branch is merged into the release branch, create annotated tag (vX.Y.Z), for example `git tag -s -m "v4.0.0" v4.0.0`
- backport release through `release_backport.py` script 