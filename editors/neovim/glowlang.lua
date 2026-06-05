-- ============================================================
-- GlowLang Neovim Configuration
-- ============================================================
--
-- Installation:
--   1. Copy this file to ~/.config/nvim/lua/glowlang.lua
--   2. Add `require('glowlang')` to your init.lua
--   3. Ensure `glowlang` binary is in your PATH
--   4. Install nvim-lspconfig: https://github.com/neovim/nvim-lspconfig
--
-- Requirements:
--   - Neovim >= 0.8.0
--   - nvim-lspconfig plugin
--   - (Optional) nvim-cmp for autocompletion
--
-- ============================================================

local M = {}

-- ============================================================
-- Filetype Detection
-- ============================================================
vim.filetype.add({
    extension = {
        glow = "glowlang",
    },
    pattern = {
        [".*%.glow"] = "glowlang",
    },
})

-- ============================================================
-- Syntax Highlighting (vim regex-based)
-- ============================================================
vim.api.nvim_create_autocmd("FileType", {
    pattern = "glowlang",
    callback = function()
        local buf = vim.api.nvim_get_current_buf()

        -- Keywords
        vim.cmd([[
            syntax keyword glowKeyword show write when whenever eif otherwise
            syntax keyword glowKeyword while until repeat for each in
            syntax keyword glowKeyword func lambda return stop skip end
            syntax keyword glowKeyword let try catch throw class trait extends
            syntax keyword glowKeyword use spawn true false times input
            syntax keyword glowKeyword is is_not from to every blueprint action can
            syntax keyword glowKeyword and or not

            " Built-in functions
            syntax keyword glowBuiltin len type range append push pop sort reverse
            syntax keyword glowBuiltin keys values has remove merge upper lower trim
            syntax keyword glowBuiltin find replace starts_with ends_with split

            " Module names
            syntax keyword glowModule os math fs json http tensor

            " Boolean literals
            syntax keyword glowBoolean true false

            " Comments
            syntax match glowComment "\/\/.*$"
            syntax region glowComment start="/\*" end="\*/"

            " Strings
            syntax region glowString start=/"/ skip=/\\"/ end=/"/
            syntax region glowString start=/'/ skip=/\\'/ end=/'/
            syntax region glowFString start=/f"/ skip=/\\"/ end=/"/
            syntax region glowFString start=/f'/ skip=/\\'/ end=/'/

            " Numbers
            syntax match glowNumber "\<\d\+\>"
            syntax match glowNumber "\<\d\+\.\d*\>"
            syntax match glowNumber "\<\.\d\+\>"

            " Operators
            syntax match glowOperator "[\+\-\*/%=<>!&|^~]"
            syntax match glowOperator "\*\*"
            syntax match glowOperator "=="
            syntax match glowOperator "!="
            syntax match glowOperator "<="
            syntax match glowOperator ">="
            syntax match glowOperator "+="
            syntax match glowOperator "-="
            syntax match glowOperator "\*="
            syntax match glowOperator "/="

            " Function definitions
            syntax match glowFuncDef "\<func\s\+\zs\w\+"
            syntax match glowClassDef "\<class\s\+\zs\w\+"
            syntax match glowClassDef "\<blueprint\s\+\zs\w\+"
            syntax match glowClassDef "\<trait\s\+\zs\w\+"

            " Highlight groups
            highlight default link glowKeyword    Keyword
            highlight default link glowBuiltin    Function
            highlight default link glowModule     Type
            highlight default link glowBoolean    Boolean
            highlight default link glowComment    Comment
            highlight default link glowString     String
            highlight default link glowFString    String
            highlight default link glowNumber     Number
            highlight default link glowOperator   Operator
            highlight default link glowFuncDef    Function
            highlight default link glowClassDef   Type
        ]])

        -- Set buffer options
        vim.bo[buf].commentstring = "// %s"
        vim.bo[buf].tabstop = 4
        vim.bo[buf].shiftwidth = 4
        vim.bo[buf].expandtab = true
    end,
})

-- ============================================================
-- LSP Configuration
-- ============================================================
function M.setup_lsp()
    -- Try to use nvim-lspconfig if available
    local ok, lspconfig = pcall(require, "lspconfig")
    if not ok then
        vim.notify(
            "[GlowLang] nvim-lspconfig not found. Install it for LSP support.",
            vim.log.levels.WARN
        )
        return
    end

    local configs = require("lspconfig.configs")

    -- Register glowlang as a custom LSP server
    if not configs.glowlang then
        configs.glowlang = {
            default_config = {
                cmd = { "glowlang", "--lsp" },
                filetypes = { "glowlang" },
                root_dir = function(fname)
                    return lspconfig.util.find_git_ancestor(fname)
                        or lspconfig.util.path.dirname(fname)
                end,
                single_file_support = true,
                settings = {},
            },
        }
    end

    -- Start the LSP server with capabilities
    lspconfig.glowlang.setup({
        on_attach = function(client, bufnr)
            -- Key mappings
            local opts = { noremap = true, silent = true, buffer = bufnr }

            -- Go to definition
            vim.keymap.set("n", "gd", vim.lsp.buf.definition, opts)

            -- Hover documentation
            vim.keymap.set("n", "K", vim.lsp.buf.hover, opts)

            -- Trigger completion manually
            vim.keymap.set("i", "<C-Space>", vim.lsp.buf.completion, opts)

            -- Show references
            vim.keymap.set("n", "gr", vim.lsp.buf.references, opts)

            -- Rename symbol
            vim.keymap.set("n", "<leader>rn", vim.lsp.buf.rename, opts)

            -- Code action
            vim.keymap.set("n", "<leader>ca", vim.lsp.buf.code_action, opts)

            -- Show diagnostics
            vim.keymap.set("n", "[d", vim.diagnostic.goto_prev, opts)
            vim.keymap.set("n", "]d", vim.diagnostic.goto_next, opts)
            vim.keymap.set("n", "<leader>e", vim.diagnostic.open_float, opts)

            vim.notify(
                "[GlowLang] LSP attached to buffer " .. bufnr,
                vim.log.levels.INFO
            )
        end,

        capabilities = vim.lsp.protocol.make_client_capabilities(),
    })
end

-- ============================================================
-- Auto-setup on module load
-- ============================================================
M.setup_lsp()

return M
