(note: this is a temporary file, to be added-to by anybody, and moved to
release-notes at release time)

Notable changes
===============

Removal of time adjustment
-------------------------------------------------------------

Prior to v2.0.21, `zend` would adjust the local time that it used by up
to 70 minutes, according to a median of the times sent by the first 200 peers
to connect to it. This mechanism was inherently insecure, since an adversary
making multiple connections to the node could effectively control its time
within that +/- 70 minute window (this is called a "timejacking attack").

In the v2.0.21 release, in addition to other mitigations for timejacking attacks,
as a simplification the time adjustment code has now been completely removed.
Node operators should instead simply ensure that local time is set
reasonably accurately.

If it appears that the node has a significantly different time than its peers,
a warning will still be logged and indicated on the metrics screen if enabled.
