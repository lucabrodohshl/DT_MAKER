import pandas as pd
import numpy as np
import config


def table2_latex(df, path):

    refs = {'A': r'\ref{scenario:a}', 'B': r'\ref{scenario:b}', 'C': r'\ref{scenario:c}'}
    probs = sorted(df['prob'].unique())
    line1 = "& ".join(
        f"\\multicolumn{{3}}{{c}}{{{int(a*100)}\\%}}"
        for a in probs
        ) + r"\\"
    line2 = "& ".join(
        f"\\scriptsize {{SR}}& \\scriptsize {{DR}} & \\scriptsize {{AR}}"
        for a in probs
        ) + r"\\"
    line3 = "& ".join(
        f"Compl. & Done"
        for a in probs
        ) + r"\\"
    line4 = " ".join(
        f"\\cmidrule(lr){{{a}-{a+2}}}" 
        for a in range(2,len(probs)*3,3)
    )
    c_string = "|"
    for i in range(len(probs)):
        c_string += "c" * 3
        c_string += "|"

    latex = r"""\begin{table}[t]
\centering
\caption{Success rates (SR), depletion rates (DR) and abort rates (AR) for each scenario across all battery stress probability levels.}
\label{tab:sweep_battery_outcomes}
  \setlength{\tabcolsep}{1pt}
\begin{tabular}{@{}p{1.4cm}"""+  c_string + r"""@{}}
\toprule
\multirow{4}{*}{Scenario}    & \multicolumn{"""+ str(len(probs)*3) +r"""}{c}{Battery stress probability} \\
\cmidrule(lr){2-19}
&""" + line1 + """
""" + line4 + r"""
&""" + line2 + r"""
\midrule
"""
    for scenario in ['A', 'B', 'C']:
        cells = [refs[scenario]]

        for prob in probs:
            row = df[df['prob'] == prob].iloc[0]

            compl = row[f'complete_{scenario}']
            viol  = row[f'viol_{scenario}']

            if scenario in ("A", "C"):
                ab = "0"
            else:
                ab = f"{row[f'mid_abort_{scenario}']:.0f}"

            cells.extend([
                f"{compl:.0f}",
                f"{viol:.0f}",
                ab
            ])
        latex += " & ".join(cells) + r"\\"
        latex += """
"""
    latex += """
"""    
    latex += r"""\bottomrule
\end{tabular}

\end{table}
"""

    with open(path, 'w') as f:
        f.write(latex)
    print(f'  [Table 1 LaTeX] {path}')





df = pd.read_csv('results/sweep_results.csv')
table2_latex(df, 'results/table2.tex')