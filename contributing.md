# Contributing Guidelines

Thank you for considering contributing to this project! Please follow the guidelines below to ensure a smooth contribution process.

## Getting Started
1. **Fork the Repository**: Click the "Fork" button on the repository’s page.
2. **Clone Your Fork**: Run the following command:
   ```sh
   git clone https://github.com/pe1mew/windmeters-modbus-interface-tester-.git
   ```
3. **Create a Branch**: Use a descriptive branch name for your changes:
   ```sh
   git checkout -b feature/your-feature-name
   ```

## Making Changes
- Follow the project's coding style and guidelines.
- Firmware changes: run the host-side unit tests before submitting — `pio test -e native` from the `firmware/` directory (no hardware required). New library code should come with a matching test suite in `firmware/test/`.
- Keep commits focused and meaningful.
- Write clear commit messages following conventional commit format.
- Update documentation if applicable.

## Submitting a Pull Request
1. **Push to Your Fork**:
   ```sh
   git push origin feature/your-feature-name
   ```
2. **Open a Pull Request**:
   - Navigate to the original repository.
   - Click on "New Pull Request".
   - Select your branch and provide a clear description of your changes.

## Code Review Process
- PRs will be reviewed by maintainers.
- Be open to feedback and make necessary changes.
- Ensure your branch is up to date with the latest main branch before merging.

## Reporting Issues
- Check if the issue has already been reported.
- Provide detailed information, including steps to reproduce the issue.
- Use clear and concise language.

## Community Standards
- Follow the [Code of Conduct](code_of_conduct.md).
- Be respectful and collaborative.

Happy Coding! 🚀

