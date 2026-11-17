# Owner Authorization

Owner Authorization is performed by the [central Core workflow](https://github.com/djconnect-music-dj/djconnect/actions/workflows/owner-authorization.yml). The former repository-local manual workflow has been removed; use the central workflow's **Run workflow** action instead. The link itself does not grant approval.

For a pull request requiring authorization:

1. Obtain explicit owner approval for the exact current pull-request commit.
2. Confirm that Trusted Delivery passed for that same commit.
3. In the central workflow on `main`, provide:
   - `repository`: `djconnect-music-dj/djconnect-esp32`
   - `pr_number`: the pull-request number
   - `candidate_sha`: the full current pull-request head SHA
   - `branch`: the pull request's target branch
4. Confirm that the central run succeeds and that the exact commit has a successful **Owner Authorization** status created by `djconnect-org-owner-authorization[bot]`. Recheck the pull-request head and all required checks before merging.

A changed commit requires a new qualification and authorization. The central workflow enforces its own actor, repository, branch, commit and Trusted Delivery checks; no authorization credentials are stored in this repository.
