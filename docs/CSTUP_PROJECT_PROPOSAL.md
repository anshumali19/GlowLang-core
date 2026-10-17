# COUNCIL OF SCIENCE AND TECHNOLOGY, UTTAR PRADESH (CSTUP)
## ENGINEERING STUDENT PROJECT GRANT SCHEME
### OFFICIAL PROJECT PROPOSAL & GRANT APPLICATION DOSSIER

---

**PROJECT TITLE:**  
**Design and Development of an Indigenous Memory-Safe Systems Programming Runtime with Integrated Anti-Hacking Guardrails and Standalone Neural Network Inference**

**ACRONYM / WORKING TITLE:**  
**GlowLang Project**

**APPLIED SCHEME:**  
C.S.T.U.P., Engineering Student Project Grant Scheme  
(सी.एस.टी., यू.पी., अभियांत्रिकी विद्यार्थी प्रोजेक्ट अनुदान योजना)

**SUBMISSION TO:**  
Council of Science and Technology, Uttar Pradesh (विज्ञान एवं प्रौद्योगिकी परिषद, उ०प्र०)  
Vigyan Bhawan, 09, Nabiullah Road, Suraj Kund Park, Lucknow - 226018

**TARGETED PRIORITY AREAS (PER CST UP CLAUSE 2):**  
- **Primary Sector (Sector iv):** Computer and Mobile Software Anti-Hacking and Data Protection (कम्प्यूटर एवं मोबाइल सॉफ्टवेयर हेतु एन्टी-हैकिंग एवं डाटा संरक्षण)
- **Secondary Sector (Sector xi):** Solution of a Current Burning Technological Problem (वर्तमान की किसी ज्वलंत समस्या का समाधान)

**ACADEMIC SESSION:** 2026 - 2027  
**COURSE:** Bachelor of Technology (B.Tech) - Final Year  
**DEPARTMENT:** Department of Computer Science & Engineering / Information Technology  

---

## SECTION 1: APPLICANT AND INSTITUTIONAL PARTICULARS

### 1.1 Student Team Details (2-Member Team)

| Parameter | Student 1 (Group Leader) | Student 2 (Team Member) |
| :--- | :--- | :--- |
| **Full Name** | [Male Student Full Name] | [Female Student Full Name] |
| **University Roll Number** | [Roll Number] | [Roll Number] |
| **Branch / Semester** | B.Tech CSE / IT (Final Year) | B.Tech CSE / IT (Final Year) |
| **Aggregate Marks (up to 3rd Year)** | [e.g., 78.5%] (Must be >= 65%) | [e.g., 81.2%] (Must be >= 65%) |
| **Gender** | Male | Female |
| **Email Address** | [Email Address] | [Email Address] |
| **Contact Mobile Number** | [Phone Number] | [Phone Number] |
| **Bank Account Details** | [Account No, Bank Name, IFSC] | [Account No, Bank Name, IFSC] |

*Note: Per CST UP Clause 15, the sanctioned grant amount will be transferred to the bank account of Student 1 (Group Leader).*

### 1.2 Institutional and Faculty Details

| Parameter | Details |
| :--- | :--- |
| **Project Guide / Supervisor** | [Faculty Name, Designation, Department] |
| **Faculty Guide Email & Phone** | [Guide Email, Mobile Number] |
| **Head of Department (HOD)** | [HOD Name, Department of CSE/IT] |
| **Director / Principal Name** | [Director / Principal Full Name] |
| **College / University Name** | [Full Name of Engineering Institution] |
| **College Address & District** | [Campus Address, District, Uttar Pradesh - PIN] |
| **College Affiliation** | Dr. A.P.J. Abdul Kalam Technical University (AKTU) / State University |

---

## SECTION 2: MANDATORY CERTIFICATES AND UNDERTAKINGS
*(Prescribed under CST UP Guidelines Clauses 11, 12, and 15)*

### 2.1 Undertaking by Participating Students (Clause 11)
We, the undersigned final year B.Tech students, hereby certify that:
1. The project proposal titled *"Design and Development of an Indigenous Memory-Safe Systems Programming Runtime with Integrated Anti-Hacking Guardrails and Standalone Neural Network Inference"* is an original technological initiative conceived by us.
2. This proposal is not a copy, duplication, or re-presentation of any project carried out by senior students or prior batches.
3. We have developed a basic preliminary proof-of-concept for syntax analysis, and we propose to utilize the 3-month CST UP grant period to fully design, build, test, and validate the complete anti-hacking runtime and neural inference subsystem.
4. If any information is found incorrect or duplicated, our application shall be disqualified immediately.

Signature of Student 1 (Group Leader): ___________________________ Date: ______________  
Signature of Student 2 (Team Member):  ___________________________ Date: ______________  

---

### 2.2 Institutional Endorsement and Undertaking by Principal / Director & HOD (Clause 11 & 12)
It is certified that:
1. The applicants are bonafide final-year B.Tech students of this institution having an aggregate of >= 65% marks up to their third year.
2. The project proposal has been scrutinized and verified to be an original student-led initiative under the supervision of the nominated faculty guide.
3. The institution will oversee the judicious, transparent, and systematic utilization of the sanctioned grant of Rs. 20,000/-.
4. The institution shall ensure that the students complete the deliverables within the stipulated 3-month window and present their working model and technical demonstration before the CST UP Reviewer Committee at Vigyan Bhawan, Lucknow.

Signature of Project Guide: ___________________________  
Name & Seal:  

Signature of Head of Department (HOD): ___________________________  
Name & Seal:  

Signature of Director / Principal: ___________________________  
Official College Seal & Date:  

---

## SECTION 3: PROJECT OVERVIEW AND PROBLEM FORMULATION

### 3.1 Background of the Study
The digital infrastructure of Uttar Pradesh and the nation relies heavily on two contrasting software paradigms:
1. **Unsafe Low-Level Languages (C/C++)**: These provide raw computational speed but suffer from fundamental memory management vulnerabilities (buffer overflows, dangling pointers, memory leaks). Modern cyber attacks exploit these weaknesses to execute unauthorized arbitrary code on municipal and government servers.
2. **Heavyweight High-Level Languages (Python)**: Modern AI and data systems rely on Python wrappers. However, Python requires multi-gigabyte environments, complex toolchains, and foreign-managed dependencies. This creates heavy deployment barriers on low-cost edge computers, smart traffic sensors, and local administrative terminals.

### 3.2 Problem Statement
There is a pressing need for an **indigenous, lightweight, and memory-safe systems programming language** that provides native protection against hacking exploits while simultaneously enabling standalone machine learning and neural network execution on commodity hardware with zero external dependencies.

### 3.3 Proposed Solution
The proposed project, **GlowLang**, introduces a sovereign programming language runtime featuring:
- An ultra-fast bytecode execution engine with memory-safe boundary checks.
- Integrated anti-hacking guardrails (`GlowGuard` and `Kavach`) that eliminate buffer overflows at runtime.
- A pure native Transformer neural network inference pipeline running directly on multi-dimensional tensors without requiring Python, PyTorch, or third-party wrappers.
- A completely self-contained, portable binary distribution package requiring no pre-installed compilers, MSYS2, or external software frameworks.

---

## SECTION 4: CURRENT STATUS (PRELIMINARY PROOF-OF-CONCEPT)

To ensure high feasibility and eliminate implementation risks within the 3-month CST UP schedule, the student team has established a **rudimentary initial proof-of-concept (PoC)**:

1. **Theoretical Formalism**: Syntactic grammar rules and token specifications have been drafted.
2. **Basic Prototype Lexer/Parser**: A minimal lexer and parser prototype capable of scanning elementary tokens and constructing a basic Abstract Syntax Tree (AST) for simple arithmetic operations.
3. **Execution Feasibility Study**: A feasibility analysis demonstrating that a register-based bytecode execution pipeline can eliminate memory stack overhead.

**What Remains to be Developed During the 3-Month CST UP Grant Phase:**
- Full development of the memory protection security subsystem (`GlowGuard`).
- Design and implementation of the data isolation memory subsystem (`Kavach`).
- Creation of the native multi-dimensional tensor computation engine.
- Implementation of the pure Transformer architecture (self-attention, multi-head attention, layer norm, numerical softmax).
- Engineering the zero-dependency portable binary packaging system.
- Systematic benchmarking against industry baselines and authoring an IEEE/Scopus indexed research paper.

---

## SECTION 5: METHODOLOGY AND PROPOSED SYSTEM ARCHITECTURE

```
+-------------------------------------------------------------+
|                 GlowLang Source Code (.glow)                |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|               Lexer & Parser (Flex / Bison)                 |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                 Abstract Syntax Tree (AST)                  |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|            Bytecode Compiler & Register Allocator           |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|                 Dual-Mode Execution Core                    |
|          [Register Virtual Machine]   [JIT Compiler]        |
+-------------------------------------------------------------+
                              |
                              v
+-------------------------------------------------------------+
|             Glow Security & AI Engine Subsystem             |
|   - GlowGuard (Boundary Checking & Buffer Overflow Trap)    |
|   - Kavach (Memory Isolation & Anti-Tampering Engine)       |
|   - Native Pure Transformer (Self-Attention & Softmax)      |
+-------------------------------------------------------------+
```

### 5.1 Anti-Hacking and Security Subsystems (Sector iv)
1. **GlowGuard Boundary Guarding**:
   - Intercepts all array indexing and memory pointer operations.
   - Compares target offsets against allocated boundary descriptors.
   - Halts invalid read/write attempts with structured diagnostics, preventing heap corruption and arbitrary code injection.
2. **Kavach Data Isolation Architecture**:
   - Segregates critical execution state (system credentials, cryptographic keys) into isolated memory partitions.
   - Forbids arbitrary pointer arithmetic in user-level applications.
3. **Bytecode Integrity Verification**:
   - Embeds cryptographic hash verification (SHA-256) into compiled bytecode modules, neutralizing unauthorized tampering or injection.

### 5.2 Pure Native Transformer AI Architecture (Sector xi)
The neural network inference module will be formulated entirely within GlowLang's native tensor system:
1. **Sinusoidal Positional Encoding**: Invariant position vectors computed via sine and cosine mathematical series.
2. **Scaled Dot-Product Attention**:
   $$\text{Attention}(Q, K, V) = \text{softmax}\left(\frac{Q K^T}{\sqrt{d_k}}\right) V$$
3. **Numerically Stable Softmax**:
   $$\text{softmax}(z_i) = \frac{\exp(z_i - \max(z))}{\sum_{j} \exp(z_j - \max(z))}$$
   Employs a high-precision Taylor series approximation to prevent floating-point underflow/overflow.
4. **Multi-Head Self-Attention & Layer Normalization**: Fully integrated encoder layers for sequence classification and feature extraction.

### 5.3 Zero-Dependency Standalone Packaging
A primary deliverable of the project is a portable standalone bundle (under 10 MB) containing:
- Pre-compiled executable binaries (`glowlang.exe`, package manager `gdu.exe`).
- Self-contained native dynamic libraries (crypto, SQLite).
- Complete standard library modules.
- End-users will be able to extract and execute `.glow` programs on standard Windows and Linux operating systems with zero prior setup.

---

## SECTION 6: THREE-MONTH WORK PLAN AND MILESTONES (CST UP CLAUSE 7)

The project will be executed over exactly 3 months following sanction:

| Month | Weeks | Planned Technical Activities | Target Milestone Deliverable |
| :--- | :--- | :--- | :--- |
| **Month 1** | Week 1 - 2 | Extend prototype AST parser into a complete register-based bytecode compiler. Implement core virtual machine opcodes. | Functional bytecode VM execution engine. |
| | Week 3 - 4 | Design and integrate `GlowGuard` array boundary validation and `Kavach` memory isolation logic. | Anti-hacking memory protection subsystem. |
| **Month 2** | Week 5 - 6 | Implement native multi-dimensional tensor data structures, matrix operations, and vector arithmetic. | High-performance tensor computation engine. |
| | Week 7 - 8 | Construct pure GlowLang Transformer layers (self-attention, multi-head attention, layer norm, sequence classifier). | Working Transformer neural network module. |
| **Month 3** | Week 9 - 10 | Assemble zero-dependency standalone distribution package. Conduct performance benchmarking against Python and C baselines. | Tested portable distribution bundle and benchmark data. |
| | Week 11 - 12 | Draft research manuscript for Scopus/IEEE indexing. Prepare Utilization Certificate, presentation slides, and final demonstration for CST UP Reviewer Committee. | Final project dossier and CST UP review readiness. |

---

## SECTION 7: PROPOSED BUDGET BREAKDOWN (RS. 20,000/- GRANT)
*(Per CST UP Clauses 8, 10, and 15)*

| S.No. | Budget Category | Technical Justification | Amount (INR) |
| :--- | :--- | :--- | :--- |
| 1 | **Cloud Virtual Testing Instances** | Provisioning multi-platform virtual machines (Windows x64, Ubuntu Linux, macOS) for automated cross-compilation testing and stress testing. | Rs. 6,500.00 |
| 2 | **Security Fuzzing and Profiling Utilities** | Specialized fuzz testing storage, memory leak detection tools, and security benchmarking licenses. | Rs. 4,500.00 |
| 3 | **Research Publication & Indexing Charges** | Partial support for manuscript formatting, similarity indexing, and Scopus/IEEE journal conference submission fees. | Rs. 4,000.00 |
| 4 | **Technical Documentation & Hardbound Binding** | Official project dossiers, institutional submission portfolios, and high-resolution architecture diagrams. | Rs. 2,000.00 |
| 5 | **Travel & Presentation Contingency** | Travel and local conveyance for both students to present the project before the Reviewer Committee at Vigyan Bhawan, Lucknow (Clause 9 & 17). | Rs. 3,000.00 |
| | **Total Proposed Budget** | **Exact Grant Allocation** | **Rs. 20,000.00** |

---

## SECTION 8: RESEARCH PAPER PUBLICATION PLAN (CST UP CLAUSES 18-21)

Under CST UP clauses 18-21, a publication incentive of **Rs. 5,000/- per student author** (total **Rs. 10,000/-** for our 2-member student team) is awarded upon publishing in an SCI / Scopus / IEEE indexed journal.

- **Proposed Paper Title:**  
  *"Architecting an Indigenous Memory-Safe Systems Language with Native Attention-Based Transformer Inference for Resource-Constrained Environments"*
- **Nominated Student Authors:**  
  1. [Male Student Name] (Primary Author)  
  2. [Female Student Name] (Co-Author)  
- **Target Journal Venues:**  
  1. *IEEE Access* (IEEE / Scopus Indexed)  
  2. *Journal of Systems Architecture* (Elsevier / SCI / Scopus Indexed)  
  3. *Innovations in Systems and Software Engineering* (Springer / Scopus Indexed)  
- **Expected Submission Timeline:**  
  Within 60 days following the project demonstration at Vigyan Bhawan, Lucknow.

---

## SECTION 9: SOCIETAL RELEVANCE AND IMPACT

### 9.1 Relevance to Uttar Pradesh
1. **Protection of Civic Software (Sector iv)**: Eliminating buffer overflows and unauthorized memory tampering in state municipal portals, billing databases, and public distribution systems.
2. **Smart City & Traffic Optimization (Sector viii & x)**: Providing a lightweight software engine capable of running AI detection and sensor logic directly on low-power road junction devices across UP cities without operating system crashes.
3. **Democratizing Technical Education**: Permitting students in state technical institutions with basic hardware to study deep learning and compiler design without requiring expensive GPU servers.

### 9.2 National Technological Independence (Atmanirbhar Bharat)
The development of an indigenous programming language contributes directly to self-reliance in fundamental software infrastructure, reducing long-term dependence on foreign proprietary ecosystems.

---

## SECTION 10: EXPECTED DELIVERABLES AT CST UP EVALUATION

Upon completion of the 3-month grant period, the team will present the following deliverables to the Reviewer Committee at Vigyan Bhawan, Lucknow:
1. **Working Software Demonstration**: Live compilation and execution of memory-safe GlowLang programs.
2. **Live Transformer Inference Demo**: Real-time forward pass classification executing in ~1 millisecond on a standard laptop with zero Python dependencies.
3. **Portable Distribution Package**: Standalone, double-click/terminal runnable package under 10 MB.
4. **Comprehensive Technical Documentation & Source Code Dossier**.
5. **Verified Utilization Certificate (UC) and Acknowledgement Receipt** endorsed by the Head of Department.
6. **Completed Research Manuscript** prepared for Scopus/IEEE submission.
