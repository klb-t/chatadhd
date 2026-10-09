import unittest
from run_index import render


class IndexContract(unittest.TestCase):
    def test_missing_dependency_is_error(self):
        with self.assertRaisesRegex(ValueError, 'native_lib'):
            render({'argv': ['probe', '{repo}', '{native_lib}']}, {'repo': '/tmp/repo'})

    def test_path_is_single_argv_and_not_shell_expansion(self):
        path = '/tmp/synthetic folder/$(not-a-command);fixture'
        self.assertEqual(render({'argv': ['probe', '--repo', '{repo}']}, {'repo': path}),
                         ['probe', '--repo', path])

    def test_sha_is_explicit_not_silently_defaulted(self):
        with self.assertRaisesRegex(ValueError, 'sha'):
            render({'argv': ['probe', '--sha', '{sha}']}, {})


if __name__ == '__main__':
    unittest.main()
