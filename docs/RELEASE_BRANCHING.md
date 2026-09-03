# Release and maintenance branches

- `develop` is the integration branch and never creates a production release.
- `main` is the stable branch. A release is created only from a protected
  version tag such as `v1.1.0` whose commit is reachable from `main`.
- `release/v1.1.x` is the optional maintenance line for compatible patch fixes.
- `hotfix/*` branches are short-lived and are merged into the applicable
  maintenance line and `develop` after validation.
- Security fixes follow the private disclosure process in `SECURITY.md`.
- A patch release (`1.1.x`) must remain API and ABI compatible. New compatible
  public capabilities require a minor release; incompatible changes require a
  new major release.
