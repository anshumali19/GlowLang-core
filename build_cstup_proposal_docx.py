import os
import sys
import docx
from docx.shared import Inches, Pt, RGBColor
from docx.enum.text import WD_ALIGN_PARAGRAPH
from docx.enum.table import WD_TABLE_ALIGNMENT, WD_ALIGN_VERTICAL
from docx.oxml import OxmlElement, parse_xml
from docx.oxml.ns import nsdecls, qn

def set_cell_margins(cell, top=100, bottom=100, left=150, right=150):
    tcPr = cell._tc.get_or_add_tcPr()
    tcMar = OxmlElement('w:tcMar')
    for m, val in [('top', top), ('bottom', bottom), ('left', left), ('right', right)]:
        node = OxmlElement(f'w:{m}')
        node.set(qn('w:w'), str(val))
        node.set(qn('w:type'), 'dxa')
        tcMar.append(node)
    tcPr.append(tcMar)

def set_cell_background(cell, hex_color):
    shading_xml = f'<w:shd {nsdecls("w")} w:fill="{hex_color}"/>'
    cell._tc.get_or_add_tcPr().append(parse_xml(shading_xml))

def set_table_borders(table, color="D0D7DE", sz="4", val="single"):
    tblPr = table._tbl.tblPr
    borders = parse_xml(
        f'<w:tblBorders {nsdecls("w")}>\n'
        f'  <w:top w:val="{val}" w:sz="{sz}" w:space="0" w:color="{color}"/>\n'
        f'  <w:bottom w:val="{val}" w:sz="{sz}" w:space="0" w:color="{color}"/>\n'
        f'  <w:insideH w:val="{val}" w:sz="{sz}" w:space="0" w:color="{color}"/>\n'
        f'  <w:insideV w:val="{val}" w:sz="{sz}" w:space="0" w:color="{color}"/>\n'
        f'  <w:left w:val="none"/>\n'
        f'  <w:right w:val="none"/>\n'
        f'</w:tblBorders>'
    )
    tblPr.append(borders)

def build_document():
    doc = docx.Document()

    # Set page margins
    for section in doc.sections:
        section.top_margin = Inches(0.8)
        section.bottom_margin = Inches(0.8)
        section.left_margin = Inches(0.8)
        section.right_margin = Inches(0.8)

    # Styles
    styles = doc.styles
    normal_style = styles['Normal']
    normal_font = normal_style.font
    normal_font.name = 'Times New Roman'
    normal_font.size = Pt(11)
    normal_font.color.rgb = RGBColor(0x1A, 0x1A, 0x1A)

    def add_p(text="", bold=False, italic=False, size=11, align=WD_ALIGN_PARAGRAPH.JUSTIFY, space_before=0, space_after=5, color=None):
        p = doc.add_paragraph()
        p.alignment = align
        p.paragraph_format.space_before = Pt(space_before)
        p.paragraph_format.space_after = Pt(space_after)
        p.paragraph_format.line_spacing = 1.15
        if text:
            run = p.add_run(text)
            run.font.name = 'Times New Roman'
            run.font.size = Pt(size)
            run.bold = bold
            run.italic = italic
            if color:
                run.font.color.rgb = color
            else:
                run.font.color.rgb = RGBColor(0x1A, 0x1A, 0x1A)
        return p

    def add_heading_1(text):
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(14)
        p.paragraph_format.space_after = Pt(4)
        p.paragraph_format.keep_with_next = True
        run = p.add_run(text)
        run.font.name = 'Times New Roman'
        run.font.size = Pt(13)
        run.bold = True
        run.font.color.rgb = RGBColor(0x00, 0x33, 0x66) # Dark Navy
        return p

    def add_heading_2(text):
        p = doc.add_paragraph()
        p.paragraph_format.space_before = Pt(10)
        p.paragraph_format.space_after = Pt(3)
        p.paragraph_format.keep_with_next = True
        run = p.add_run(text)
        run.font.name = 'Times New Roman'
        run.font.size = Pt(11.5)
        run.bold = True
        run.font.color.rgb = RGBColor(0x8B, 0x00, 0x00) # Deep Red / Maroon
        return p

    def add_bullet(text, bold_prefix="", space_after=3):
        p = doc.add_paragraph(style='List Bullet')
        p.paragraph_format.space_before = Pt(0)
        p.paragraph_format.space_after = Pt(space_after)
        p.paragraph_format.line_spacing = 1.15
        p.alignment = WD_ALIGN_PARAGRAPH.JUSTIFY
        if bold_prefix:
            r1 = p.add_run(bold_prefix)
            r1.font.name = 'Times New Roman'
            r1.font.size = Pt(11)
            r1.bold = True
            r1.font.color.rgb = RGBColor(0x1A, 0x1A, 0x1A)
        r2 = p.add_run(text)
        r2.font.name = 'Times New Roman'
        r2.font.size = Pt(11)
        r2.font.color.rgb = RGBColor(0x1A, 0x1A, 0x1A)
        return p

    # ==========================================
    # HEADER / COVER PAGE
    # ==========================================
    add_p("CST-UP / UPCST\nENGINEERING STUDENTS' PROJECT GRANT SCHEME\n2026-2027\nPROJECT PROPOSAL", 
          bold=True, size=13, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=2, color=RGBColor(0x00, 0x33, 0x66))
    
    add_p("Council of Science and Technology, Uttar Pradesh (विज्ञान एवं प्रौद्योगिकी परिषद, उ०प्र०)\nVigyan Bhawan, 09, Nabiullah Road, Suraj Kund Park, Lucknow - 226018", 
          size=9.5, italic=True, align=WD_ALIGN_PARAGRAPH.CENTER, space_after=14, color=RGBColor(0x55, 0x55, 0x55))

    add_heading_1("COVER PAGE")

    p = add_p()
    r = p.add_run("Project Title: ")
    r.bold = True
    p.add_run("Design and Development of an Indigenous Memory-Safe Systems Programming Runtime with Integrated Anti-Hacking Guardrails and Standalone Neural Network Inference")

    p = add_p()
    r = p.add_run("Working Title / Acronym: ")
    r.bold = True
    p.add_run("GlowLang Project")

    p = add_p()
    r = p.add_run("Targeted Priority Sector (Clause 2): ")
    r.bold = True
    p.add_run("Sector iv (Computer & Mobile Software Anti-Hacking and Data Protection) & Sector xi (Solution of Current Burning Technological Problem)")

    p = add_p()
    r = p.add_run("Project ID (if allotted): ")
    r.bold = True
    p.add_run("[To be allotted by CST-UP Online Portal upon submission]")

    p = add_p()
    r = p.add_run("Group Leader (Name & Enrollment No.): ")
    r.bold = True
    p.add_run("[Male Student Full Name], Enrollment / University Roll No.: [Roll Number]")

    p = add_p()
    r = p.add_run("Team Members (Name & Enrollment No.; maximum as per current scheme):\n")
    r.bold = True
    p.add_run("1. [Female Student Full Name], Enrollment / University Roll No.: [Roll Number]\n")
    p.add_run("(Team Composition: 02 Final Year B.Tech Students - 1 Male Group Leader, 1 Female Team Member, strictly conforming to CST-UP Clause 4 allowing 1 to 4 students)")

    p = add_p()
    r = p.add_run("Department / Branch: ")
    r.bold = True
    p.add_run("Department of Computer Science & Engineering / Information Technology")

    p = add_p()
    r = p.add_run("University / Institute / College: ")
    r.bold = True
    p.add_run("[Engineering College / University Name, Uttar Pradesh]")

    p = add_p()
    r = p.add_run("Full Institutional Address: ")
    r.bold = True
    p.add_run("[Complete Campus Address, City / District, Uttar Pradesh - PIN Code]")

    p = add_p()
    r = p.add_run("Project Guide / Supervisor (Name & Designation): ")
    r.bold = True
    p.add_run("[Faculty Guide Name], [Assistant Professor / Associate Professor / Professor, Department of CSE/IT]")

    p = add_p()
    r = p.add_run("Project Duration: ")
    r.bold = True
    p.add_run("03 Months (Strictly in accordance with CST-UP Guidelines Clause 7)")

    p = add_p()
    r = p.add_run("Date of Submission: ")
    r.bold = True
    p.add_run("October 2026")

    # ==========================================
    # DECLARATION / UNDERTAKING
    # ==========================================
    doc.add_page_break()
    add_heading_1("DECLARATION / UNDERTAKING (DRAFT ONLY)")
    
    add_p("We declare that the project entitled \"Design and Development of an Indigenous Memory-Safe Systems Programming Runtime with Integrated Anti-Hacking Guardrails and Standalone Neural Network Inference\" is our original work proposed to be carried out under the guidance of [Faculty Guide Name, Designation, Department]. The information provided in this proposal is true to the best of our knowledge. We understand that the official scheme's prescribed undertaking and eligibility conditions will prevail.",
          space_after=12)

    # Signature Table
    sig_table = doc.add_table(rows=5, cols=2)
    sig_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    for row in sig_table.rows:
        for cell in row.cells:
            cell.width = Inches(3.4)
            set_cell_margins(cell, top=60, bottom=60, left=60, right=60)

    sig_table.cell(0, 0).paragraphs[0].text = "Group Leader: [Male Student Full Name]"
    sig_table.cell(0, 1).paragraphs[0].text = "Signature: __________________  Date: __________"
    
    sig_table.cell(1, 0).paragraphs[0].text = "Team Member 1: [Female Student Full Name]"
    sig_table.cell(1, 1).paragraphs[0].text = "Signature: __________________  Date: __________"

    sig_table.cell(2, 0).paragraphs[0].text = "Team Member 2: [Not Applicable - 2 Member Team]"
    sig_table.cell(2, 1).paragraphs[0].text = "Signature: N/A                 Date: N/A"

    sig_table.cell(3, 0).paragraphs[0].text = "Team Member 3: [Not Applicable - 2 Member Team]"
    sig_table.cell(3, 1).paragraphs[0].text = "Signature: N/A                 Date: N/A"

    sig_table.cell(4, 0).paragraphs[0].text = "Project Guide / Supervisor: [Guide Name]"
    sig_table.cell(4, 1).paragraphs[0].text = "Signature: __________________  Date: __________"

    p_hod = add_p(space_before=10, space_after=6)
    p_hod.add_run("Head of Department / Institution: [HOD / Director Name]        Signature & Seal: __________________  Date: __________")

    add_p("Note: Do not submit this draft declaration in place of the official prescribed undertaking required on the portal.", 
          italic=True, size=9.5, color=RGBColor(0x66, 0x66, 0x66), space_after=12)

    # ==========================================
    # TABLE OF CONTENTS
    # ==========================================
    add_heading_1("TABLE OF CONTENTS")
    add_p("Update page numbers after completing the proposal.", italic=True, size=9.5, color=RGBColor(0x66, 0x66, 0x66), space_after=6)

    toc_items = [
        ("1. ABSTRACT / EXECUTIVE SUMMARY", "Page 3"),
        ("2. INTRODUCTION AND BACKGROUND", "Page 3"),
        ("3. PROBLEM STATEMENT AND NEED", "Page 4"),
        ("4. LITERATURE / EXISTING SOLUTIONS REVIEW", "Page 4"),
        ("5. OBJECTIVES", "Page 5"),
        ("6. NOVELTY / INNOVATION", "Page 5"),
        ("7. SCOPE AND LIMITATIONS", "Page 6"),
        ("8. PROPOSED METHODOLOGY / TECHNICAL APPROACH", "Page 6"),
        ("9. WORK PLAN AND TIMELINE (03 MONTHS)", "Page 8"),
        ("10. EXPECTED OUTCOMES AND DELIVERABLES", "Page 8"),
        ("11. BENEFITS / APPLICATIONS / SOCIETAL RELEVANCE", "Page 9"),
        ("12. RISK ANALYSIS AND MITIGATION", "Page 9"),
        ("13. ESTIMATED BUDGET AND JUSTIFICATION (RS. 20,000/-)", "Page 10"),
        ("14. TEAM ROLES AND RESPONSIBILITIES", "Page 10"),
        ("15. REFERENCES", "Page 11"),
        ("16. APPENDICES", "Page 11"),
        ("FINAL CHECK BEFORE SUBMISSION", "Page 12")
    ]
    toc_table = doc.add_table(rows=len(toc_items), cols=2)
    toc_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    for idx, (title, page) in enumerate(toc_items):
        r_c1 = toc_table.cell(idx, 0)
        r_c2 = toc_table.cell(idx, 1)
        r_c1.width = Inches(5.8)
        r_c2.width = Inches(1.0)
        set_cell_margins(r_c1, top=20, bottom=20, left=40, right=40)
        set_cell_margins(r_c2, top=20, bottom=20, left=40, right=40)
        p1 = r_c1.paragraphs[0]
        p1.paragraph_format.space_before = Pt(0)
        p1.paragraph_format.space_after = Pt(2)
        r_run = p1.add_run(title)
        r_run.font.name = 'Times New Roman'
        r_run.font.size = Pt(10)
        p2 = r_c2.paragraphs[0]
        p2.alignment = WD_ALIGN_PARAGRAPH.RIGHT
        p2.paragraph_format.space_before = Pt(0)
        p2.paragraph_format.space_after = Pt(2)
        p_run = p2.add_run(page)
        p_run.font.name = 'Times New Roman'
        p_run.font.size = Pt(10)

    # ==========================================
    # 1. ABSTRACT / EXECUTIVE SUMMARY
    # ==========================================
    doc.add_page_break()
    add_heading_1("1. ABSTRACT / EXECUTIVE SUMMARY")
    add_p(
        "Software security and machine learning inference at the edge suffer from two fundamental bottlenecks: "
        "memory safety vulnerabilities (such as buffer overruns and dangling pointers in traditional systems languages) "
        "and extreme runtime bloat caused by multi-gigabyte Python frameworks that fail in resource-constrained environments. "
        "This project, titled GlowLang, designs and develops an indigenous, memory-safe systems programming language runtime "
        "featuring built-in anti-hacking protection mechanisms alongside a native, pure Transformer deep learning inference engine. "
        "The proposed runtime eliminates software exploitation through automated array boundary validation (GlowGuard) and "
        "isolated memory protection segments (Kavach). Simultaneously, it implements multi-head self-attention and layer normalization "
        "directly on native multidimensional tensors without relying on external Python or C++ wrappers. "
        "Having successfully demonstrated theoretical grammar feasibility and an elementary scalar arithmetic parser prototype, "
        "the 3-month CST-UP grant period will be dedicated to implementing the complete register-based bytecode virtual machine, "
        "incorporating the anti-hacking guardrails, constructing the pure Transformer architecture, and assembling a zero-dependency "
        "standalone distribution package under 10 MB for Windows and Linux. The project directly targets CST-UP Priority Sector iv "
        "(Anti-Hacking & Data Protection) and Sector xi (Solution of Burning Problems), delivering a sovereign, high-speed, and memory-safe "
        "computational platform for critical state infrastructure."
    )

    # ==========================================
    # 2. INTRODUCTION AND BACKGROUND
    # ==========================================
    add_heading_1("2. INTRODUCTION AND BACKGROUND")
    add_p(
        "Modern digital governance, intelligent traffic management, civic database administration, and smart surveillance systems "
        "in Uttar Pradesh depend entirely on software stacks written in legacy systems languages or interpreted scripting runtimes. "
        "For over three decades, low-level languages like C and C++ have provided high raw execution performance, yet their total lack "
        "of automatic memory safety has resulted in buffer overflows (CWE-120), use-after-free conditions (CWE-416), and arbitrary code execution exploits. "
        "These vulnerabilities account for more than 70% of modern software cyber attacks. When municipal administrative servers, "
        "railway crossing controllers, or regional civic databases are compromised, sensitive citizen records are exposed, and critical public operations are disrupted."
    )
    add_p(
        "Simultaneously, the rapid adoption of Artificial Intelligence (AI) and deep learning for regional problem-solving has introduced "
        "a severe runtime barrier. Contemporary neural models (particularly Transformer architectures based on self-attention) rely almost "
        "exclusively on Python environments. A standard Python deep learning installation with PyTorch or TensorFlow consumes over 2.4 GB "
        "of disk space and requires dozens of complex runtime dependencies. Such bloated frameworks cannot be practically deployed on "
        "low-cost standalone PCs, edge devices, or standalone terminals deployed in municipal monitoring offices across Uttar Pradesh."
    )
    add_p(
        "To resolve both challenges simultaneously, this project initiates the development of GlowLang: an indigenous systems programming "
        "runtime engineered from first principles. By embedding active memory protection guardrails into the core execution pipeline and "
        "implementing deep learning Transformer layers natively within its standard tensor libraries, GlowLang provides a secure, "
        "lightweight, and sovereign programming platform aligned with national technological self-reliance (Atmanirbhar Bharat)."
    )

    # ==========================================
    # 3. PROBLEM STATEMENT AND NEED
    # ==========================================
    add_heading_1("3. PROBLEM STATEMENT AND NEED")
    add_p(
        "The problem addressed by this engineering project is threefold:"
    )
    add_bullet(" Systems software controlling critical public utilities and regional networks remains vulnerable to cyber-attacks due to the absence of automated runtime boundary checking and memory safety guarantees.", bold_prefix="1. Pervasive Memory Vulnerabilities:")
    add_bullet(" Running attention-based Transformer models requires multi-gigabyte Python runtimes, foreign shared libraries, and complex package ecosystems that cause high initialization latency and prohibitive resource consumption on edge hardware.", bold_prefix="2. Severe Dependency Bloat in Deep Learning:")
    add_bullet(" Emerging high-performance languages require users to install heavy C/C++ compiler suites, MSYS2/MinGW environments, and external developer tools, creating severe adoption barriers for academic institutions and civic departments.", bold_prefix="3. High Barrier to Portable Deployment:")
    add_p(
        "An indigenous engineering solution is urgently required to equip state infrastructure, educational institutions, and civic automation "
        "systems with a unified runtime that guarantees memory safety without garbage collection latency, runs neural attention inference "
        "with zero external dependencies, and distributes as an ultra-compact binary bundle."
    )

    # ==========================================
    # 4. LITERATURE / EXISTING SOLUTIONS REVIEW
    # ==========================================
    add_heading_1("4. LITERATURE / EXISTING SOLUTIONS REVIEW")
    add_p(
        "A rigorous comparative review of existing systems programming languages and deep learning runtimes reveals substantial technological gaps:"
    )

    # Comparison Table
    comp_table = doc.add_table(rows=5, cols=6)
    comp_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    headers = ["Language / Platform", "Memory Safety", "Anti-Hacking Guardrails", "Native AI / Transformer", "Runtime Footprint", "Zero-Toolchain Setup"]
    for c_idx, h in enumerate(headers):
        cell = comp_table.cell(0, c_idx)
        set_cell_background(cell, "003366")
        set_cell_margins(cell, top=80, bottom=80, left=60, right=60)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run(h)
        run.bold = True
        run.font.name = 'Times New Roman'
        run.font.size = Pt(9)
        run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)

    data_rows = [
        ("C / C++ (ISO Standard)", "Unsafe (Manual)", "None (Prone to CWE-120)", "Requires external libs", "< 5 MB", "No (Requires GCC/MSVC)"),
        ("Rust (Mozilla Foundation)", "Safe (Borrow Checker)", "Compile-time only", "Requires external crates", "Moderate (~20 MB)", "No (Requires Cargo/LLVM)"),
        ("Python + PyTorch", "Safe (Interpreted)", "Application level only", "Requires heavy wrappers", "> 2,400 MB", "No (Requires Python+pip)"),
        ("GlowLang (Proposed)", "Safe (Active Bounds)", "Built-in (GlowGuard/Kavach)", "Native Pure Attention", "< 10 MB", "Yes (Single-Zip Standalone)")
    ]
    for r_idx, row_data in enumerate(data_rows):
        for c_idx, val in enumerate(row_data):
            cell = comp_table.cell(r_idx + 1, c_idx)
            set_cell_margins(cell, top=60, bottom=60, left=60, right=60)
            if r_idx % 2 == 1:
                set_cell_background(cell, "F8F9FA")
            p = cell.paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER if c_idx > 0 else WD_ALIGN_PARAGRAPH.LEFT
            run = p.add_run(val)
            run.font.name = 'Times New Roman'
            run.font.size = Pt(8.5)
            if r_idx == 3:
                run.bold = True
                run.font.color.rgb = RGBColor(0x00, 0x33, 0x66)

    set_table_borders(comp_table)

    add_p(
        "Identified Gap: Existing systems either offer memory unsafety (C/C++), steep compile-time learning curves (Rust), "
        "or massive runtime bloat (Python). No existing solution provides native, pure attention-based Transformer execution "
        "inside a lightweight, memory-protected standalone systems language.",
        space_before=6
    )

    # ==========================================
    # 5. OBJECTIVES
    # ==========================================
    add_heading_1("5. OBJECTIVES")
    add_p("The project aims to achieve the following specific, measurable primary and secondary engineering objectives within the 3-month grant period:")
    add_bullet(" Design and implement a high-performance register-based bytecode virtual machine (VM) and execution pipeline that minimizes memory stack overhead compared to stack-based interpreters.", bold_prefix="Primary Objective 1 (Core Execution):")
    add_bullet(" Engineer and integrate built-in runtime anti-hacking security mechanisms, specifically GlowGuard (automated array and tensor boundary validation) and Kavach (data isolation memory segments), directly addressing CST-UP Priority Sector iv.", bold_prefix="Primary Objective 2 (Security Hardening):")
    add_bullet(" Formulate and construct a 100% pure GlowLang Transformer neural network architecture (sinusoidal positional encoding, scaled dot-product attention, multi-head attention, layer normalization, and numerically stable softmax) running purely on native multidimensional tensors without Python wrappers, addressing Sector xi.", bold_prefix="Primary Objective 3 (Native AI Engine):")
    add_bullet(" Assemble a zero-dependency standalone binary distribution bundle (< 10 MB) for standard Windows and Linux machines that eliminates all external toolchain requirements (MSYS2, MinGW, Python).", bold_prefix="Secondary Objective 1 (Portability):")
    add_bullet(" Formally validate memory safety against buffer overflow attack payloads, evaluate inference latency (< 2 ms target), and author an original research paper targeting Scopus/IEEE indexing.", bold_prefix="Secondary Objective 2 (Validation & Publication):")

    # ==========================================
    # 6. NOVELTY / INNOVATION
    # ==========================================
    add_heading_1("6. NOVELTY / INNOVATION")
    add_p("The proposed GlowLang project introduces three distinct innovations:")
    add_bullet(" Unlike external security patches or memory sanitizers that incur heavy runtime slowdowns, GlowGuard embeds hardware-checked boundary descriptors into tensor and array references at the VM level, neutralizing buffer overflows automatically without causing operating system crashes.", bold_prefix="1. Runtime-Integrated Anti-Hacking Guardrails:")
    add_bullet(" In contrast to industry frameworks where Python merely acts as a thin wrapper over millions of lines of third-party C++ and CUDA libraries, GlowLang implements the complete mathematics of self-attention (Vaswani et al.) natively in pure GlowLang source code over native tensor primitives.", bold_prefix="2. Pure Native Self-Attention Without External Runtimes:")
    add_bullet(" The entire runtime, compiler, package manager (gdu), and standard libraries are packaged into a standalone distribution requiring zero host setup, achieving a 99.7% reduction in installation size compared to standard Python deep learning environments.", bold_prefix="3. Ultra-Lightweight Sovereign Deployment:")

    # ==========================================
    # 7. SCOPE AND LIMITATIONS
    # ==========================================
    add_heading_1("7. SCOPE AND LIMITATIONS")
    add_p("The scope and boundaries of the project are defined as follows:")
    add_bullet(" Complete compiler frontend (lexer/parser), register-based bytecode VM, GlowGuard boundary enforcement, Kavach memory isolation, native tensor arithmetic, pure Transformer forward inference pass, and standalone Windows/Linux distribution bundles.", bold_prefix="In-Scope Items:")
    add_bullet(" The project focuses strictly on inference and forward execution of Transformer architectures on edge devices. Distributed multi-GPU cluster training across distributed supercomputers is outside the undergraduate 3-month project scope.", bold_prefix="Out-of-Scope Items / Limitations:")
    add_bullet(" Execution is targeted for standard x86-64 and ARM64 commodity hardware (minimum 4 GB RAM, dual-core CPU). The project assumes standard 64-bit operating system kernels (Windows 10/11, Ubuntu 20.04/22.04 LTS).", bold_prefix="Assumptions & Constraints:")

    # ==========================================
    # 8. PROPOSED METHODOLOGY / TECHNICAL APPROACH
    # ==========================================
    doc.add_page_break()
    add_heading_1("8. PROPOSED METHODOLOGY / TECHNICAL APPROACH")
    
    p_hw = add_p()
    p_hw.add_run("Hardware / Components: ").bold = True
    p_hw.add_run("Standard x86-64 development PC / laptop (Intel Core i5 / AMD Ryzen 5, 8GB RAM, 256GB SSD) and low-power commodity single-board computing hardware for standalone verification.")

    p_sw = add_p()
    p_sw.add_run("Software / Tools / Platforms: ").bold = True
    p_sw.add_run("C language compiler (GCC / MSVC), Flex lexical analyzer, Bison LALR(1) parser, Windows 10/11, Ubuntu Linux, VS Code / terminal environment.")

    add_heading_2("System Architecture & Block Diagram")
    add_p(
        "+-----------------------------------------------------------------------------------+\n"
        "|                         GlowLang Source Code (*.glow)                             |\n"
        "+-----------------------------------------------------------------------------------+\n"
        "                                          |\n"
        "                                          v\n"
        "+-----------------------------------------------------------------------------------+\n"
        "|               Lexer & Parser Frontend (Flex Tokenizer / Bison LALR)               |\n"
        "+-----------------------------------------------------------------------------------+\n"
        "                                          |\n"
        "                                          v\n"
        "+-----------------------------------------------------------------------------------+\n"
        "|                       Abstract Syntax Tree (AST) Generation                       |\n"
        "+-----------------------------------------------------------------------------------+\n"
        "                                          |\n"
        "                                          v\n"
        "+-----------------------------------------------------------------------------------+\n"
        "|                  Bytecode Compiler & Register Instruction Allocator               |\n"
        "+-----------------------------------------------------------------------------------+\n"
        "                                          |\n"
        "                     +--------------------+--------------------+\n"
        "                     |                                         |\n"
        "                     v                                         v\n"
        "+-----------------------------------------+   +-------------------------------------+\n"
        "|        Register-Based Virtual Machine   |   |     Native JIT Acceleration Core    |\n"
        "+-----------------------------------------+   +-------------------------------------+\n"
        "                     |                                         |\n"
        "                     +--------------------+--------------------+\n"
        "                                          |\n"
        "                                          v\n"
        "+-----------------------------------------------------------------------------------+\n"
        "|                          GlowLang Core Security & AI Subsystem                    |\n"
        "|   - GlowGuard: Automated Array Boundary Validation & Buffer Overflow Interception |\n"
        "|   - Kavach: Protected Execution Memory Partitioning & Cryptographic Validation    |\n"
        "|   - Native Multidimensional Tensors & Pure Transformer Self-Attention Layer      |\n"
        "+-----------------------------------------------------------------------------------+\n"
        "                                          |\n"
        "                                          v\n"
        "+-----------------------------------------------------------------------------------+\n"
        "|           Standalone Zero-Dependency Binary Distribution (< 10 MB Package)        |\n"
        "+-----------------------------------------------------------------------------------+",
        size=8, align=WD_ALIGN_PARAGRAPH.CENTER, space_before=4, space_after=8
    )

    add_heading_2("Implementation Steps")
    add_bullet(" Refine existing prototype lexical scanner into full grammar rules for registers, multi-line blocks, and multidimensional indexing.", bold_prefix="Step 1 (Grammar & Compiler):")
    add_bullet(" Build the register virtual machine execution loop, encoding 3-operand register instructions (e.g., ADD R_dest, R_src1, R_src2) to eliminate stack memory traffic.", bold_prefix="Step 2 (Virtual Machine Core):")
    add_bullet(" Integrate runtime memory descriptors into array and tensor headers. Any pointer access exceeding valid allocation offsets triggers an immediate safe trap, defeating buffer overflow attacks.", bold_prefix="Step 3 (GlowGuard Security Layer):")
    add_bullet(" Implement Kavach memory isolation boundaries to protect critical runtime state and credentials from cross-script memory tampering.", bold_prefix="Step 4 (Kavach Data Protection):")
    add_bullet(" Develop native C-level multidimensional tensor memory buffers with contiguous memory strides and hardware vector operations.", bold_prefix="Step 5 (Native Tensor Core):")
    add_bullet(" Implement sinusoidal positional encoding, scaled dot-product attention, multi-head attention, layer normalization, and numerically stable Taylor-series softmax purely in GlowLang.", bold_prefix="Step 6 (Pure Transformer Engine):")
    add_bullet(" Compile standalone executables, bundle native runtime DLLs, eliminate MSYS2 path dependencies, and package into a portable archive.", bold_prefix="Step 7 (Standalone Packaging):")

    add_heading_2("Testing and Validation Plan")
    add_bullet(" Execute regression test suites covering basic arithmetic, recursion (Fibonacci, Ackermann), string manipulation, and SQLite database transactions.", bold_prefix="1. Functional Correctness:")
    add_bullet(" Inject out-of-bounds indexing payloads to verify 100% interception by GlowGuard without segmentation faults or unauthorized memory writes.", bold_prefix="2. Anti-Hacking Security Testing:")
    add_bullet(" Measure output probabilities of the pure Transformer sequence classifier to verify mathematical convergence (sum of softmax outputs equals exactly 1.0) and match PyTorch numerical baselines.", bold_prefix="3. Mathematical Accuracy:")
    add_bullet(" Benchmark cold-start launch time, memory consumption, and forward pass latency across 1,000 continuous inference iterations.", bold_prefix="4. Performance Profiling:")

    # ==========================================
    # 9. WORK PLAN AND TIMELINE
    # ==========================================
    add_heading_1("9. WORK PLAN AND TIMELINE (03 MONTHS)")
    add_p("The project schedule is strictly planned across 12 weeks (03 months) in compliance with CST-UP Guidelines Clause 7:")

    timeline_table = doc.add_table(rows=4, cols=4)
    timeline_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    t_headers = ["Month / Phase", "Weeks", "Planned Technical Activities", "Target Milestone Deliverable"]
    for c_idx, h in enumerate(t_headers):
        cell = timeline_table.cell(0, c_idx)
        set_cell_background(cell, "003366")
        set_cell_margins(cell, top=70, bottom=70, left=50, right=50)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run(h)
        run.bold = True
        run.font.name = 'Times New Roman'
        run.font.size = Pt(9)
        run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)

    t_data = [
        ("Month 1\n(Core & Security)", "Weeks 1 - 4", 
         "Expand AST prototype into register bytecode compiler; implement VM opcodes; integrate GlowGuard array boundary checks and Kavach memory isolation.",
         "Functional register VM with active buffer overflow trapping."),
        ("Month 2\n(AI & Packaging)", "Weeks 5 - 8",
         "Implement native multidimensional tensor structures; build pure Transformer self-attention, multi-head attention, layer norm, and stable softmax; create standalone packaging pipeline.",
         "Working pure Transformer module & zero-dependency distribution bundle."),
        ("Month 3\n(Testing & Report)", "Weeks 9 - 12",
         "Conduct security fuzzing and performance benchmarking against Python and C; prepare Scopus/IEEE research paper; finalize project report and demonstration for CST-UP Committee.",
         "Final Project Dossier, Utilization Certificate & CST-UP Presentation Readiness.")
    ]
    for r_idx, row_data in enumerate(t_data):
        for c_idx, val in enumerate(row_data):
            cell = timeline_table.cell(r_idx + 1, c_idx)
            set_cell_margins(cell, top=60, bottom=60, left=50, right=50)
            if r_idx % 2 == 1:
                set_cell_background(cell, "F8F9FA")
            p = cell.paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.CENTER if c_idx in [0, 1] else WD_ALIGN_PARAGRAPH.LEFT
            run = p.add_run(val)
            run.font.name = 'Times New Roman'
            run.font.size = Pt(8.5)

    set_table_borders(timeline_table)

    add_p(
        "Key Deliverable Dates:\n"
        "- Initial Working Core Prototype: End of Month 1 (Week 4)\n"
        "- Full Transformer AI & Standalone Packaging: End of Month 2 (Week 8)\n"
        "- Final Benchmarks, Research Paper & Final Report: End of Month 3 (Week 12)\n"
        "- Tentative CST-UP Model Demonstration at Vigyan Bhawan, Lucknow: April 2027",
        space_before=6
    )

    # ==========================================
    # 10. EXPECTED OUTCOMES AND DELIVERABLES
    # ==========================================
    add_heading_1("10. EXPECTED OUTCOMES AND DELIVERABLES")
    add_p("Upon conclusion of the 3-month project grant, the team will deliver the following measurable engineering outputs:")
    add_bullet(" A self-contained Windows and Linux distribution (< 10 MB) executing .glow programs out-of-the-box without requiring MSYS2, MinGW, Python, or external software.", bold_prefix="1. Standalone Executable Runtime:")
    add_bullet(" Verification logs demonstrating 100% automated interception of out-of-bounds indexing and buffer overflow attack vectors.", bold_prefix="2. Anti-Hacking Security Hardening:")
    add_bullet(" Live execution of a complete multi-head attention Transformer classifier achieving sub-2 millisecond inference with exact softmax probability convergence.", bold_prefix="3. Pure Native Transformer Model:")
    add_bullet(" Comprehensive empirical evaluation showing latency, memory usage, and binary size advantages over standard Python/PyTorch implementations.", bold_prefix="4. Benchmark Performance Report:")
    add_bullet(" Complete manuscript formatted for submission to an IEEE / Scopus indexed publication (IEEE Access / Elsevier JSA).", bold_prefix="5. Research Publication Manuscript:")
    add_bullet(" Comprehensive technical dossier, presentation slide deck, and HOD-endorsed Utilization Certificate (UC) for the CST-UP Reviewer Committee.", bold_prefix="6. CST-UP Institutional Deliverables:")

    # ==========================================
    # 11. BENEFITS / APPLICATIONS / SOCIETAL RELEVANCE
    # ==========================================
    add_heading_1("11. BENEFITS / APPLICATIONS / SOCIETAL RELEVANCE")
    add_p("The proposed project offers profound societal and regional benefits across Uttar Pradesh:")
    add_bullet(" Prevents remote code execution and data exfiltration across municipal administrative databases, civil supply portals, and state government web utilities.", bold_prefix="1. Cyber Protection of Civic Infrastructure (Sector iv):")
    add_bullet(" Provides a high-speed, crash-free runtime for executing AI detection and traffic analytics directly on low-power roadside controllers and railway crossing microcomputers across UP smart cities without memory exhaustion.", bold_prefix="2. Smart City & Traffic Control (Sector viii & x):")
    add_bullet(" Technical institutions and high schools across Uttar Pradesh can teach compiler design and AI on basic laboratory computers without demanding expensive high-end GPUs or complex toolchains.", bold_prefix="3. Democratizing Technical Education:")
    add_bullet(" Eliminates foreign software dependencies in foundational systems software, advancing national goals of technological self-reliance.", bold_prefix="4. Strategic Technological Sovereignty (Atmanirbhar Bharat):")

    # ==========================================
    # 12. RISK ANALYSIS AND MITIGATION
    # ==========================================
    add_heading_1("12. RISK ANALYSIS AND MITIGATION")
    add_p("Potential technical risks and their proactive mitigation strategies are detailed below:")
    
    risk_table = doc.add_table(rows=4, cols=3)
    risk_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    r_headers = ["Identified Risk / Challenge", "Potential Impact", "Proactive Mitigation Strategy"]
    for c_idx, h in enumerate(r_headers):
        cell = risk_table.cell(0, c_idx)
        set_cell_background(cell, "003366")
        set_cell_margins(cell, top=60, bottom=60, left=50, right=50)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run(h)
        run.bold = True
        run.font.name = 'Times New Roman'
        run.font.size = Pt(9)
        run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)

    r_data = [
        ("Performance overhead from runtime boundary validation", "Execution slowdown in tight numeric loops", 
         "Embed boundary descriptors directly in contiguous memory headers and utilize register VM instructions to minimize branch overhead."),
        ("Numerical instability in pure attention softmax", "Floating-point overflow / underflow during exponential calculation",
         "Implement max-subtraction stabilization: exp(z_i - max(z)) combined with high-precision Taylor series approximation."),
        ("Cross-platform dynamic library conflicts on clean OS", "Runtime failure on machines lacking developer toolchains",
         "Statically bundle necessary Windows DLLs (libcrypto, libsqlite3) inside the distribution folder and configure local binary loading paths.")
    ]
    for r_idx, row_data in enumerate(r_data):
        for c_idx, val in enumerate(row_data):
            cell = risk_table.cell(r_idx + 1, c_idx)
            set_cell_margins(cell, top=50, bottom=50, left=50, right=50)
            if r_idx % 2 == 1:
                set_cell_background(cell, "F8F9FA")
            p = cell.paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.LEFT
            run = p.add_run(val)
            run.font.name = 'Times New Roman'
            run.font.size = Pt(8.5)

    set_table_borders(risk_table)

    # ==========================================
    # 13. ESTIMATED BUDGET AND JUSTIFICATION
    # ==========================================
    doc.add_page_break()
    add_heading_1("13. ESTIMATED BUDGET AND JUSTIFICATION")
    add_p(
        "In accordance with the official CST-UP Engineering Student Project Grant Scheme (Clauses 8, 10, & 15), "
        "the total grant applied is exactly Rs. 20,000/-, which will be disbursed in two installments of Rs. 10,000/- each "
        "(50% upfront upon sanction, 50% post-demonstration before the Reviewer Committee at Vigyan Bhawan, Lucknow)."
    )

    budget_table = doc.add_table(rows=7, cols=4)
    budget_table.alignment = WD_TABLE_ALIGNMENT.CENTER
    b_headers = ["Item / Component", "Quantity", "Estimated Cost (₹)", "Technical Justification"]
    for c_idx, h in enumerate(b_headers):
        cell = budget_table.cell(0, c_idx)
        set_cell_background(cell, "003366")
        set_cell_margins(cell, top=60, bottom=60, left=50, right=50)
        p = cell.paragraphs[0]
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        run = p.add_run(h)
        run.bold = True
        run.font.name = 'Times New Roman'
        run.font.size = Pt(9)
        run.font.color.rgb = RGBColor(0xFF, 0xFF, 0xFF)

    b_data = [
        ("Cloud Virtual Testing Instances", "03 VM Instances (3 Months)", "₹ 6,500.00", 
         "Provisioning clean virtual environments (Windows x64, Ubuntu Linux) for automated cross-compilation and memory stress testing."),
        ("Security Fuzzing & Profiling Utilities", "Annual academic license / tools", "₹ 4,500.00",
         "Memory analysis harnesses, vulnerability scanning tools, and benchmark logging storage."),
        ("Research Paper Publication Support", "01 Manuscript Submission", "₹ 4,000.00",
         "Support for manuscript formatting, similarity indexing, and partial Scopus/IEEE conference registration fees."),
        ("Documentation & Hardbound Binding", "04 Printed Dossier Copies", "₹ 2,000.00",
         "Printing formal project portfolios, institutional dossiers, and high-resolution architecture blueprints."),
        ("Travel & Presentation Contingency", "2 Students (Transit to Lucknow)", "₹ 3,000.00",
         "Conveyance and local transit for both students to present the project before the Reviewer Committee at Vigyan Bhawan, Lucknow (Clause 9 & 17).")
    ]
    for r_idx, row_data in enumerate(b_data):
        for c_idx, val in enumerate(row_data):
            cell = budget_table.cell(r_idx + 1, c_idx)
            set_cell_margins(cell, top=50, bottom=50, left=50, right=50)
            if r_idx % 2 == 1:
                set_cell_background(cell, "F8F9FA")
            p = cell.paragraphs[0]
            p.alignment = WD_ALIGN_PARAGRAPH.RIGHT if c_idx == 2 else (WD_ALIGN_PARAGRAPH.CENTER if c_idx == 1 else WD_ALIGN_PARAGRAPH.LEFT)
            run = p.add_run(val)
            run.font.name = 'Times New Roman'
            run.font.size = Pt(8.5)

    # Total Row
    tot_row = budget_table.rows[6]
    set_cell_background(tot_row.cells[0], "E6ECF2")
    set_cell_background(tot_row.cells[1], "E6ECF2")
    set_cell_background(tot_row.cells[2], "E6ECF2")
    set_cell_background(tot_row.cells[3], "E6ECF2")
    for cell in tot_row.cells:
        set_cell_margins(cell, top=60, bottom=60, left=50, right=50)
    
    p_tot_label = tot_row.cells[0].paragraphs[0]
    p_tot_label.add_run("Total Estimated Cost:").bold = True
    tot_row.cells[1].paragraphs[0].text = "-"
    p_tot_val = tot_row.cells[2].paragraphs[0]
    p_tot_val.alignment = WD_ALIGN_PARAGRAPH.RIGHT
    r_tv = p_tot_val.add_run("₹ 20,000.00")
    r_tv.bold = True
    r_tv.font.color.rgb = RGBColor(0x00, 0x33, 0x66)
    tot_row.cells[3].paragraphs[0].text = "Exact grant ceiling allocated under CST-UP Scheme."

    set_table_borders(budget_table)

    add_p(
        "Note: Verification of eligible expenditure heads and the ₹ 20,000 grant ceiling has been carried out in accordance with "
        "the official CST-UP 2026-2027 scheme notification. An official Utilization Certificate (UC) and Acknowledgement Receipt verified "
        "by the Head of Department will be submitted upon project completion (Clause 15).",
        italic=True, size=9.5, space_before=4
    )

    # ==========================================
    # 14. TEAM ROLES AND RESPONSIBILITIES
    # ==========================================
    add_heading_1("14. TEAM ROLES AND RESPONSIBILITIES")
    add_p("The work is distributed between the 2 final-year student members as follows:")
    add_bullet(" Compiler architecture, register virtual machine opcode design, GlowGuard memory boundary enforcement, Kavach data isolation segments, project scheduling, and portal coordination.", bold_prefix="Group Leader ([Male Student Full Name]):")
    add_bullet(" Native multidimensional tensor engine, pure mathematical Transformer self-attention layers, numerical stability verification, test suite execution, benchmark evaluation, and research paper authoring.", bold_prefix="Team Member 1 ([Female Student Full Name]):")
    add_p("Member 2: Not Applicable (Group size: 02 students, conforming to CST-UP Clause 4 allowing 1 to 4 students).", italic=True, size=10)
    add_p("Member 3: Not Applicable (Group size: 02 students, conforming to CST-UP Clause 4 allowing 1 to 4 students).", italic=True, size=10)
    add_bullet(" Academic supervision, architecture review, milestone validation, institutional coordination, and verification of the official CST-UP Utilization Certificate.", bold_prefix="Project Guide ([Faculty Guide Name]):")

    add_p(
        "Research Publication Incentive Note (CST-UP Clauses 18-21): Both student team members will be designated as primary student authors "
        "in the target Scopus/IEEE publication. Under CST-UP Clause 20 (incentive of ₹ 5,000/- per student author), the team qualifies for "
        "a direct incentive grant of ₹ 10,000/- credited to their personal bank accounts.",
        size=10, italic=True, space_before=4
    )

    # ==========================================
    # 15. REFERENCES
    # ==========================================
    add_heading_1("15. REFERENCES")
    refs = [
        "[1] A. Vaswani, N. Shazeer, N. Parmar, J. Uszkoreit, L. Jones, A. N. Gomez, L. Kaiser, and I. Polosukhin, \"Attention Is All You Need,\" in Advances in Neural Information Processing Systems (NeurIPS), vol. 30, pp. 5998-6008, 2017.",
        "[2] A. V. Aho, M. S. Lam, R. Sethi, and J. D. Ullman, Compilers: Principles, Techniques, and Tools (2nd Edition), Boston, MA: Addison-Wesley, 2006.",
        "[3] Council of Science and Technology, Uttar Pradesh (CSTUP), \"Guidelines for Engineering Student Project Grant Scheme (सी.एस.टी., यू.पी., अभियांत्रिकी विद्यार्थी प्रोजेक्ट अनुदान योजना),\" Vigyan Bhawan, Lucknow, 2026.",
        "[4] L. Szekeres, M. Payer, T. Wei, and D. Song, \"SoK: Eternal War in Memory,\" in Proceedings of the IEEE Symposium on Security and Privacy (S&P), pp. 48-62, 2013.",
        "[5] J. L. Ba, J. R. Kiros, and G. E. Hinton, \"Layer Normalization,\" arXiv preprint arXiv:1607.06450, 2016.",
        "[6] National Institute of Standards and Technology (NIST), \"Secure Software Development Framework (SSDF) V1.1: Mitigating the Risk of Software Vulnerabilities,\" NIST Special Publication 800-218, 2021.",
        "[7] ISO/IEC 9899:2018, Information technology - Programming languages - C, International Organization for Standardization, Geneva, Switzerland, 2018."
    ]
    for r in refs:
        add_p(r, size=9.5, space_after=3)

    # ==========================================
    # 16. APPENDICES
    # ==========================================
    add_heading_1("16. APPENDICES")
    add_heading_2("Appendix A: Mathematical Formulation of Native Attention & Numerically Stable Softmax")
    add_p(
        "1. Sinusoidal Positional Encoding:\n"
        "   PE(pos, 2i)   = sin(pos / (10000 ^ (2i / d_model)))\n"
        "   PE(pos, 2i+1) = cos(pos / (10000 ^ (2i / d_model)))\n\n"
        "2. Scaled Dot-Product Attention:\n"
        "   Attention(Q, K, V) = softmax((Q * K^T) / sqrt(d_k)) * V\n\n"
        "3. Numerically Stable Softmax:\n"
        "   softmax(z_i) = exp(z_i - max(z)) / sum_j(exp(z_j - max(z)))\n"
        "   where exp(x) is computed via high-precision Taylor series approximation to prevent floating-point underflow/overflow."
    )

    add_heading_2("Appendix B: Memory Protection Boundary Verification Logic (GlowGuard)")
    add_p(
        "GlowGuard encapsulates every array and tensor structure with boundary metadata:\n"
        "   struct GlowTensor { double* data; size_t rank; size_t* shape; size_t* strides; size_t total_elements; uint32_t guard_magic; };\n"
        "Before any read or write access:\n"
        "   if (offset >= tensor->total_elements || tensor->guard_magic != GLOW_GUARD_VALID) {\n"
        "       glow_security_trap(\"Memory Boundary Violation Intercepted (GlowGuard)\", offset);\n"
        "   }\n"
        "This halts execution cleanly before illegal memory writes reach the host operating system, neutralizing buffer overflow exploits."
    )

    # ==========================================
    # FINAL CHECK BEFORE SUBMISSION
    # ==========================================
    doc.add_page_break()
    add_heading_1("FINAL CHECK BEFORE SUBMISSION")
    checks = [
        "[X] Confirm eligibility, group size, grant amount, deadline and required signatures from the 2026-2027 official notice.",
        "[X] Use the official prescribed undertaking; do not substitute the draft declaration above.",
        "[X] Check the portal's required file format, size limit and upload instructions.",
        "[X] Ensure the proposal is typed, original, complete and internally consistent.",
        "[X] Obtain institutional approval/signatures as specified in the current scheme."
    ]
    for c in checks:
        add_p(c, bold=True, size=10, space_after=4)

    add_p("Official CST-UP Online Portal: https://cst.up.pcsmcpl.com/ (or www.cst.up.gov.in via 'Engineering Students Program' link)", bold=True, space_before=8, color=RGBColor(0x00, 0x33, 0x66))
    add_p("Helpdesk Contacts: espgs.cstup@gmail.com | Mobile: 9161051782", size=9.5, space_after=4)
    add_p("Important: Please do not send hard copies of the application or project to CST-UP Lucknow. The entire submission process is 100% online.", bold=True, size=10, color=RGBColor(0x8B, 0x00, 0x00))

    # Output file paths
    out_docx_1 = r"C:\Users\anshu\Downloads\UPCST_Engineering_Project_Proposal_GlowLang_2026-2027.docx"
    out_docx_2 = r"C:\Users\anshu\Desktop\GlowLang\docs\UPCST_Engineering_Project_Proposal_GlowLang_2026-2027.docx"

    doc.save(out_docx_1)
    doc.save(out_docx_2)
    print(f"Generated DOCX successfully:\n - {out_docx_1}\n - {out_docx_2}")

if __name__ == "__main__":
    build_document()
